'use strict'

const dgram = require('dgram')
const net = require('net')
const axios = require('axios')
const FormData = require('form-data')
const { Lame } = require('node-lame')
const { Readable } = require('stream')
require('dotenv').config()

// Environment Variables
const VF_DM_API_KEY = process.env.VF_DM_API_KEY
const WHISPER_SERVER_URL = process.env.WHISPER_SERVER_URL
const UDP_PORT = parseInt(process.env.UDP_PORT || '6980') // VBAN default port
const TCP_PORT = parseInt(process.env.TCP_PORT || '12345') // TCP server port

if (!VF_DM_API_KEY || !WHISPER_SERVER_URL) {
  console.error(
    'Missing required environment variables. Please check your .env file.'
  )
  process.exit(1)
}

// ---------------------------
// Global Buffer for Audio Data
// ---------------------------
let audioBuffer = Buffer.alloc(0)
let lastBufferSize = 0
let lastBufferClearTime = Date.now()

// Add periodic buffer status check and cleanup
setInterval(() => {
  const now = Date.now()
  if (audioBuffer.length !== lastBufferSize) {
    console.log(`Buffer status: ${audioBuffer.length} bytes`)
    lastBufferSize = audioBuffer.length
  }

  // Clear buffer if it hasn't been used in 5 minutes
  if (now - lastBufferClearTime > 5 * 60 * 1000) {
    console.log('Clearing stale audio buffer')
    audioBuffer = Buffer.alloc(0)
    lastBufferSize = 0
    lastBufferClearTime = now
  }
}, 5000)

// ---------------------------
// UDP Server (VBAN Receiver)
// ---------------------------
const udpPort = UDP_PORT // Use environment variable
const udpServer = dgram.createSocket('udp4')

udpServer.on('error', (err) => {
  console.error(`UDP server error:\n${err.stack}`)
  udpServer.close()
})

udpServer.on('message', (msg, rinfo) => {
  // Log every UDP packet immediately
  console.log(`\n[UDP] Packet received from ${rinfo.address}:${rinfo.port}`)
  console.log(`[UDP] Packet size: ${msg.length} bytes`)

  lastBufferClearTime = Date.now() // Update last activity time

  // Try to detect packet type
  if (msg.length >= 28 && msg.slice(0, 4).toString() === 'VBAN') {
    // VBAN packet processing
    try {
      const sampleRate = msg.readUInt32LE(4)
      const samplesPerFrame = msg.readUInt8(8)
      const channels = msg.readUInt8(9)
      const dataFormat = msg.readUInt8(10)
      const streamName = msg.slice(16, 24).toString().replace(/\0/g, '')

      console.log('[UDP] VBAN packet detected:')
      console.log(`  Sample Rate: ${sampleRate}Hz`)
      console.log(`  Samples/Frame: ${samplesPerFrame}`)
      console.log(`  Channels: ${channels}`)
      console.log(`  Stream: "${streamName}"`)

      const payload = msg.slice(28)
      audioBuffer = Buffer.concat([audioBuffer, payload])
      console.log(
        `[UDP] Processed VBAN packet, buffer size: ${audioBuffer.length} bytes`
      )
    } catch (e) {
      console.error('[UDP] Error processing VBAN header:', e)
    }
  } else {
    // Process as raw 16-bit PCM audio data from M5Stack (16kHz, mono)
    console.log('[UDP] Processing as raw audio data')

    // Since we're receiving raw 16-bit PCM data, we can append it directly
    // but let's validate the samples first
    const samples = new Int16Array(msg.buffer, msg.byteOffset, msg.length / 2)
    const processedData = Buffer.alloc(msg.length)
    let hasSound = false
    const threshold = 500 // Adjust this threshold for noise detection

    // Process each sample with basic noise gate and gain
    for (let i = 0; i < samples.length; i++) {
      let sample = samples[i]

      // Apply noise gate
      if (Math.abs(sample) < threshold) {
        sample = 0
      } else {
        // If we detect a sample above threshold, mark that we have sound
        hasSound = true

        // Apply gain boost
        const gain = 5.0 // Increased gain for better speech detection
        sample = Math.max(-32768, Math.min(32767, Math.round(sample * gain)))
      }

      processedData.writeInt16LE(sample, i * 2)
    }

    if (hasSound) {
      console.log('[UDP] Sound detected in packet')
    }

    audioBuffer = Buffer.concat([audioBuffer, processedData])
    console.log(`[UDP] Buffer size after raw data: ${audioBuffer.length} bytes`)
  }
})

udpServer.on('listening', () => {
  const address = udpServer.address()
  console.log(`VBAN UDP server listening on ${address.address}:${address.port}`)
  console.log(
    'Make sure your VBAN source (VoiceMeeter) is configured to stream to this address and port'
  )
  // Log more details about the UDP server
  console.log(`UDP Server Details:
    Address: ${address.address}
    Port: ${address.port}
    Family: ${address.family}
    Node.js Version: ${process.version}
    Platform: ${process.platform}
  `)
})

udpServer.bind(udpPort, '0.0.0.0') // Explicitly bind to all interfaces

// TCP Server (Transcription Request and Audio Streaming)
const tcpPort = TCP_PORT // Use environment variable
const tcpServer = net.createServer((socket) => {
  // Convert IPv6 to IPv4 if it's a mapped address
  let clientAddress = socket.remoteAddress
  if (clientAddress.startsWith('::ffff:')) {
    clientAddress = clientAddress.substring(7)
  }
  const clientPort = socket.remotePort

  console.log(`[TCP] Client connected from ${clientAddress}:${clientPort}`)
  console.log(`[TCP] Current audio buffer size: ${audioBuffer.length} bytes`)

  // Add buffer size check
  if (audioBuffer.length === 0) {
    console.error(
      `[TCP] Empty audio buffer for client ${clientAddress}:${clientPort}`
    )
    console.log(
      '[TCP] Make sure VBAN is streaming to UDP port 6980 before connecting via TCP'
    )
    socket.write(
      JSON.stringify({
        error:
          'No audio data received. Please ensure VBAN is streaming to UDP port 6980 before connecting.',
      }) + '\n'
    )
    socket.end()
    return
  }

  // Validate audio before sending to transcription
  if (!validateAudioBuffer(audioBuffer)) {
    console.log('[TCP] Audio validation failed - insufficient audio content')
    socket.write(
      JSON.stringify({
        error:
          'No significant audio content detected. Please speak louder or check your microphone.',
      }) + '\n'
    )
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
  const numChannels = 1 // Mono
  const sampleRate = 16000 // 16kHz
  const bitsPerSample = 16
  const byteRate = (sampleRate * numChannels * bitsPerSample) / 8
  const blockAlign = (numChannels * bitsPerSample) / 8
  const subChunk2Size = pcmData.length
  const chunkSize = 36 + subChunk2Size

  const header = Buffer.alloc(44)

  // RIFF header
  header.write('RIFF', 0)
  header.writeUInt32LE(chunkSize, 4)
  header.write('WAVE', 8)

  // fmt chunk
  header.write('fmt ', 12)
  header.writeUInt32LE(16, 16) // Subchunk1Size (16 for PCM)
  header.writeUInt16LE(1, 20) // AudioFormat (1 for PCM)
  header.writeUInt16LE(numChannels, 22)
  header.writeUInt32LE(sampleRate, 24)
  header.writeUInt32LE(byteRate, 28)
  header.writeUInt16LE(blockAlign, 32)
  header.writeUInt16LE(bitsPerSample, 34)

  // data chunk
  header.write('data', 36)
  header.writeUInt32LE(subChunk2Size, 40)

  // Create WAV file with header and PCM data
  return Buffer.concat([header, pcmData])
}

// Add a function to validate audio before transcription
function validateAudioBuffer(buffer) {
  if (buffer.length < 1024) {
    // Minimum size check
    console.log('[Audio] Buffer too small for speech detection')
    return false
  }

  // Check if there's any significant audio content
  const samples = new Int16Array(
    buffer.buffer,
    buffer.byteOffset,
    buffer.length / 2
  )
  let maxAmplitude = 0
  let totalEnergy = 0

  for (let i = 0; i < samples.length; i++) {
    const amplitude = Math.abs(samples[i])
    maxAmplitude = Math.max(maxAmplitude, amplitude)
    totalEnergy += amplitude
  }

  const averageEnergy = totalEnergy / samples.length
  console.log(
    `[Audio] Max amplitude: ${maxAmplitude}, Average energy: ${averageEnergy}`
  )

  // Thresholds for audio validation
  if (maxAmplitude < 1000 || averageEnergy < 100) {
    console.log('[Audio] Audio levels too low for speech detection')
    return false
  }

  return true
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
      `${WHISPER_SERVER_URL}/asr?encode=false&vad_filter=true&task=transcribe&output=json&initial_prompt=You%20analyze%20a%20conversation%20beetween%20Voiceflow%20teamates.%20Here%20is%20a%20list%20of%20possible%20names%3A%20NiKo%2C%20Nina%2C%20Henry%2C%20Daniel%2C%20Yuksel.`,
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
          Authorization: VF_DM_API_KEY,
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
