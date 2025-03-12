'use strict'

const dgram = require('dgram')
const net = require('net')
const axios = require('axios')
const FormData = require('form-data')
const { Lame } = require('node-lame')
const { Readable } = require('stream')

// ---------------------------
// Global Buffer for Audio Data
// ---------------------------
let audioBuffer = Buffer.alloc(0)

// ---------------------------
// UDP Server (VBAN Receiver)
// ---------------------------
const udpPort = 6980 // VBAN default port (adjust if needed)
const udpServer = dgram.createSocket('udp4')

udpServer.on('error', (err) => {
  console.error(`UDP server error:\n${err.stack}`)
  udpServer.close()
})

udpServer.on('message', (msg, rinfo) => {
  // Assume VBAN packet: first 28 bytes = header, rest = PCM payload
  const headerSize = 28
  if (msg.length > headerSize) {
    let payload = msg.slice(headerSize)
    audioBuffer = Buffer.concat([audioBuffer, payload])
  }
})

udpServer.on('listening', () => {
  const address = udpServer.address()
  console.log(`VBAN UDP server listening on ${address.address}:${address.port}`)
})

udpServer.bind(udpPort)

// TCP Server (Transcription Request and Audio Streaming)
const tcpPort = 12345 // Port on which the Atom Echo connects after finishing
const tcpServer = net.createServer((socket) => {
  console.log('TCP client connected for transcription/audio streaming.')

  // Add buffer size check
  if (audioBuffer.length === 0) {
    console.error('Empty audio buffer received')
    socket.write(JSON.stringify({ error: 'No audio data received' }) + '\n')
    socket.end()
    return
  }

  const wavBuffer = addWavHeader(audioBuffer)

  // Log the size of the buffer being sent
  console.log(
    `Sending ${wavBuffer.length} bytes of audio data for transcription`
  )

  transcribeAudio(wavBuffer)
    .then(async (transcription) => {
      console.log('Transcription received:', transcription)

      try {
        const voiceflowResponse = await getVoiceflowResponse(transcription)
        console.log('Voiceflow response received.')

        const audioUrl = voiceflowResponse.audioUri
        console.log('Processing audio') //, audioUrl)

        // Handle data URI
        if (audioUrl.startsWith('data:audio/mp3;base64,')) {
          const mp3Buffer = dataUriToBuffer(audioUrl)

          const decoder = new Lame({
            output: 'buffer',
            raw: false,
            bitwidth: 16,
            bitrate: 192,
            sfreq: 44.1,
            mode: 'm',
            scale: 0.98,
            quality: 0,
            preset: 'standard',
          }).setBuffer(mp3Buffer)

          try {
            await decoder.decode()
            const pcmBuffer = decoder.getBuffer()

            // Convert the buffer to 16-bit samples
            const samples = []
            for (let i = 0; i < pcmBuffer.length; i += 2) {
              samples.push(pcmBuffer.readInt16LE(i))
            }

            // Improved downsampling with simple interpolation
            const adjustedSamples = []
            const ratio = 3 // 44.1kHz to ~14.7kHz

            for (let i = 0; i < samples.length - ratio; i += ratio) {
              // Average of current and next sample for smoother transition
              const sample = Math.round(
                (samples[i] + samples[Math.min(i + 1, samples.length - 1)]) / 2
              )
              adjustedSamples.push(sample)
            }

            // Convert back to buffer
            const adjustedBuffer = Buffer.alloc(adjustedSamples.length * 2)
            adjustedSamples.forEach((sample, index) => {
              adjustedBuffer.writeInt16LE(sample, index * 2)
            })

            // Limit size if needed
            const maxSize = 480000
            const audioData = adjustedBuffer.slice(
              0,
              Math.min(adjustedBuffer.length, maxSize)
            )

            const pcmStream = new Readable()
            pcmStream.push(audioData)
            pcmStream.push(null)

            console.log('Streaming adjusted PCM audio to client...')

            // Add error handlers for both socket and stream
            socket.on('error', (error) => {
              if (error.code === 'EPIPE') {
                console.log('Client disconnected before stream finished')
              } else {
                console.error('Socket error:', error)
              }
              // Cleanup
              pcmStream.destroy()
              socket.end()
            })

            pcmStream.on('error', (error) => {
              console.error('Stream error:', error)
              socket.end()
            })

            pcmStream.pipe(socket)
            pcmStream.on('end', () => {
              console.log('Audio streaming ended.')
              socket.end()
            })
          } catch (error) {
            console.error('Error decoding MP3:', error)
            socket.write(
              JSON.stringify({ error: 'Error converting audio' }) + '\n'
            )
            socket.end()
          }
        } else {
          // Handle regular URL (fallback to original behavior)
          axios({
            method: 'get',
            url: audioUrl,
            responseType: 'stream',
          })
            .then((audioResponse) => {
              console.log('Streaming audio to client...')

              // Add error handlers here too
              socket.on('error', (error) => {
                if (error.code === 'EPIPE') {
                  console.log('Client disconnected before stream finished')
                } else {
                  console.error('Socket error:', error)
                }
                // Cleanup
                audioResponse.data.destroy()
                socket.end()
              })

              audioResponse.data.on('error', (error) => {
                console.error('Stream error:', error)
                socket.end()
              })

              audioResponse.data.pipe(socket)
              audioResponse.data.on('end', () => {
                console.log('Audio streaming ended.')
                socket.end()
              })
            })
            .catch((error) => {
              console.error('Error fetching audio stream:', error)
              socket.write(
                JSON.stringify({ error: 'Error fetching audio' }) + '\n'
              )
              socket.end()
            })
        }
      } catch (error) {
        console.error('Error processing Voiceflow response:', error)
        socket.write(
          JSON.stringify({ error: 'Error getting AI response' }) + '\n'
        )
        socket.end()
      }

      // Clear the audioBuffer for the next recording
      audioBuffer = Buffer.alloc(0)
    })
    .catch((error) => {
      console.error('Error during transcription:', error)
      socket.write(
        JSON.stringify({ error: 'Error during transcription' }) + '\n'
      )
      socket.end()
      audioBuffer = Buffer.alloc(0)
    })
})

tcpServer.on('error', (err) => {
  console.error('TCP server error:', err)
})

tcpServer.listen(tcpPort, () => {
  console.log(`TCP server listening on port ${tcpPort}`)
})

// ---------------------------
// Function: Add WAV Header
// ---------------------------
function addWavHeader(pcmData) {
  // Assumptions: 16-bit, mono, 16kHz PCM data
  const numChannels = 1
  const sampleRate = 16000
  const bitsPerSample = 16
  const byteRate = (sampleRate * numChannels * bitsPerSample) / 8
  const blockAlign = (numChannels * bitsPerSample) / 8
  const header = Buffer.alloc(44)

  header.write('RIFF', 0) // ChunkID
  header.writeUInt32LE(36 + pcmData.length, 4) // ChunkSize
  header.write('WAVE', 8) // Format
  header.write('fmt ', 12) // Subchunk1ID
  header.writeUInt32LE(16, 16) // Subchunk1Size (16 for PCM)
  header.writeUInt16LE(1, 20) // AudioFormat (1 = PCM)
  header.writeUInt16LE(numChannels, 22) // NumChannels
  header.writeUInt32LE(sampleRate, 24) // SampleRate
  header.writeUInt32LE(byteRate, 28) // ByteRate
  header.writeUInt16LE(blockAlign, 32) // BlockAlign
  header.writeUInt16LE(bitsPerSample, 34) // BitsPerSample
  header.write('data', 36) // Subchunk2ID
  header.writeUInt32LE(pcmData.length, 40) // Subchunk2Size

  return Buffer.concat([header, pcmData])
}

// ---------------------------
// Function: Call Custom ASR API
// ---------------------------
async function transcribeAudio(audioBuffer) {
  const form = new FormData()

  // Create a Readable stream from the buffer
  const audioStream = new Readable()
  audioStream.push(audioBuffer)
  audioStream.push(null)

  form.append('audio_file', audioStream, {
    filename: 'audio.wav',
    contentType: 'audio/wav',
    knownLength: audioBuffer.length,
  })

  try {
    const response = await axios.post(
      'http://5.161.85.204:9002/asr?encode=false&vad_filter=true&task=transcribe&output=json&initial_prompt=You%20analyze%20a%20conversation%20beetween%20Voiceflow%20teamates.%20Here%20is%20a%20list%20of%20possible%20names%3A%20NiKo%2C%20Nina%2C%20Henry%2C%20Daniel%2C%20Yuksel.',
      form,
      {
        headers: {
          ...form.getHeaders(),
          accept: 'application/json',
        },
        maxContentLength: Infinity,
        maxBodyLength: Infinity,
      }
    )

    // console.log('ASR API Response:', JSON.stringify(response.data, null, 2))

    // Check if we got a valid response object
    if (!response.data) {
      throw new Error('No response data received from ASR API')
    }

    // Handle empty transcription case
    if (response.data.text === '') {
      console.log('No speech detected in the audio')
      return 'I could not detect any speech in the audio. Could you please try speaking again?'
    }

    return response.data.text
  } catch (error) {
    // Add better error logging
    if (error.response) {
      console.error('ASR API Error Response:', {
        status: error.response.status,
        statusText: error.response.statusText,
        data: error.response.data,
      })
      throw new Error('Failed to transcribe audio: Server error')
    } else if (error.request) {
      console.error('ASR API Request Error:', error.message)
      throw new Error('Failed to transcribe audio: Network error')
    } else {
      console.error('ASR API Error:', error.message)
      throw error
    }
  }
}

// ---------------------------
// Function: Call Voiceflow DM API
// ---------------------------
async function getVoiceflowResponse(transcribedText) {
  try {
    const response = await axios.post(
      'https://general-runtime.voiceflow.com/state/user/niko/interact',
      {
        action: {
          type: 'event',
          payload: {
            event: {
              name: 'question',
              question: transcribedText,
            },
          },
        },
        config: {
          tts: true,
          stripSSML: true,
          stopAll: false,
          excludeTypes: ['block', 'debug', 'flow'],
        },
      },
      {
        headers: {
          Authorization: 'VF.DM.67b87c7ef120ae137705a311.eOl0HlxZ3P3LgiHY',
          'Content-Type': 'application/json',
        },
      }
    )

    // Add debug logging to see the response structure
    /* console.log(
      'Voiceflow API Response:',
      JSON.stringify(response.data, null, 2)
    ) */

    // Extract the text message and audio data from the response
    const responseData = response.data[2]?.payload
    if (!responseData) {
      throw new Error('No payload in Voiceflow response')
    }
    console.log(responseData.message)
    return {
      message: responseData.message || '',
      audioUri: responseData.audio?.src || '',
    }
  } catch (error) {
    console.error('Error calling Voiceflow API:', error)
    throw error
  }
}

// Add this helper function to convert data URI to buffer
function dataUriToBuffer(dataUri) {
  const base64Data = dataUri.split(',')[1]
  return Buffer.from(base64Data, 'base64')
}
