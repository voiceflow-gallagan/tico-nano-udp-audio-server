'use strict'

import dgram from 'dgram'
import net from 'net'
import axios from 'axios'
import FormData from 'form-data'
import { Lame } from 'node-lame'
import { Readable } from 'stream'
import 'dotenv/config'
import fs from 'fs'

// Environment Variables
const VF_DM_API_KEY = process.env.VF_DM_API_KEY
const WHISPER_SERVER_URL =
  process.env.WHISPER_SERVER_URL || 'http://whisper-asr:9000'
const UDP_PORT = parseInt(process.env.UDP_PORT || '6980')
const TCP_PORT = parseInt(process.env.TCP_PORT || '12345')
const INCLUDE_TEXT_WITH_AUDIO = process.env.INCLUDE_TEXT_WITH_AUDIO !== 'false'
const WHISPER_VAD_FILTER = process.env.WHISPER_VAD_FILTER || 'false'
const GROQ_API_KEY = process.env.GROQ_API_KEY
const USE_GROQ = process.env.USE_GROQ === 'true'
const TTS_PLAYBACK_RATE = parseFloat(process.env.TTS_PLAYBACK_RATE || '1.0') // Default to normal speed (1.0)

// Groq specific settings (used if USE_GROQ is true)
const GROQ_API_URL =
  process.env.GROQ_API_URL ||
  'https://api.groq.com/openai/v1/audio/transcriptions'
const GROQ_WHISPER_MODEL =
  process.env.GROQ_WHISPER_MODEL || 'whisper-large-v3-turbo'
const GROQ_WHISPER_LANGUAGE = process.env.GROQ_WHISPER_LANGUAGE // Optional, ISO-639-1 code (e.g., 'en', 'fr')

if (!VF_DM_API_KEY) {
  console.error(
    'Missing required environment variable VF_DM_API_KEY. Please check your .env file.'
  )
  process.exit(1)
}

if (USE_GROQ) {
  if (!GROQ_API_KEY) {
    console.error(
      'USE_GROQ is true, but GROQ_API_KEY is missing. Please check your .env file.'
    )
    process.exit(1)
  }
  console.log('Using Groq for transcription.')
  console.log(` Groq API URL: ${GROQ_API_URL}`)
  console.log(` Groq Model: ${GROQ_WHISPER_MODEL}`)
  if (GROQ_WHISPER_LANGUAGE) {
    console.log(` Groq Language: ${GROQ_WHISPER_LANGUAGE}`)
  } else {
    console.log(` Groq Language: Not specified (multilingual detection)`)
  }
} else {
  console.log(`Using local Whisper ASR service at: ${WHISPER_SERVER_URL}`)
}

console.log(`UDP server will listen on port: ${UDP_PORT}`)
console.log(`TCP server will listen on port: ${TCP_PORT}`)
console.log(`Include text with audio: ${INCLUDE_TEXT_WITH_AUDIO}`)
console.log(`TTS playback rate: ${TTS_PLAYBACK_RATE}`)

// ---------------------------
// Global Buffer for Audio Data
// ---------------------------
let audioBuffer = Buffer.alloc(0)
let lastBufferSize = 0
let lastBufferClearTime = Date.now()

// Periodic buffer status check and cleanup
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
// UDP Server (Audio Receiver)
// ---------------------------
const udpServer = dgram.createSocket('udp4')

udpServer.on('error', (err) => {
  console.error(`UDP server error:\n${err.stack}`)
  udpServer.close()
})

udpServer.on('message', (msg, rinfo) => {
  lastBufferClearTime = Date.now()

  // Process raw 16-bit PCM audio data (16kHz, mono)
  const samples = new Int16Array(msg.buffer, msg.byteOffset, msg.length / 2)
  const processedData = Buffer.alloc(msg.length)
  let hasSound = false
  const threshold = 500

  // Process each sample with noise gate and gain
  for (let i = 0; i < samples.length; i++) {
    let sample = samples[i]
    // Apply noise gate and gain
    if (Math.abs(sample) < threshold) {
      sample = 0
    } else {
      hasSound = true
      const gain = 5.0
      sample = Math.max(-32768, Math.min(32767, Math.round(sample * gain)))
    }

    processedData.writeInt16LE(sample, i * 2)
  }

  if (hasSound) {
    // console.log('[UDP] Sound detected in packet')
  }

  audioBuffer = Buffer.concat([audioBuffer, processedData])
  // console.log(`[UDP] Buffer size after raw data: ${audioBuffer.length} bytes`)
})

udpServer.on('listening', () => {
  const address = udpServer.address()
  console.log(`UDP server listening on ${address.address}:${address.port}`)
  console.log(
    'Make sure your source (Tico Nano) is configured to stream to this address and port'
  )
  console.log(`UDP Server Details:
    Address: ${address.address}
    Port: ${address.port}
    Family: ${address.family}
    Node.js Version: ${process.version}
    Platform: ${process.platform}
  `)
})

udpServer.bind(UDP_PORT, '0.0.0.0')

// ---------------------------
// TCP Server (Transcription and Response)
// ---------------------------
const tcpServer = net.createServer((socket) => {
  let clientAddress = socket.remoteAddress
  if (clientAddress.startsWith('::ffff:')) {
    clientAddress = clientAddress.substring(7)
  }
  const clientPort = socket.remotePort
  let clientConfig = { includeText: INCLUDE_TEXT_WITH_AUDIO } // Default to env variable

  console.log(`[TCP] Client connected from ${clientAddress}:${clientPort}`)
  console.log(`[TCP] Current audio buffer size: ${audioBuffer.length} bytes`)

  if (audioBuffer.length === 0) {
    console.error(
      `[TCP] Empty audio buffer for client ${clientAddress}:${clientPort}`
    )
    socket.write(
      JSON.stringify({
        error:
          'No audio data received. Please ensure audio is streaming to UDP port 6980 before connecting.',
      }) + '\n'
    )
    socket.end()
    return
  }

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

  // Set up a one-time data handler to catch client config
  socket.once('data', (data) => {
    try {
      // Try to parse as JSON first
      const jsonData = JSON.parse(data.toString().trim())
      if (typeof jsonData === 'object') {
        console.log('[TCP] Received client configuration:', jsonData)

        // Extract includeText from client config if present
        if (jsonData.hasOwnProperty('includeText')) {
          clientConfig.includeText = !!jsonData.includeText
          console.log(
            `[TCP] Client requested includeText: ${clientConfig.includeText}`
          )
        }

        // Process audio after receiving config
        processAudioTranscription()
      } else {
        // If not valid JSON object, treat as audio data
        console.log(
          '[TCP] No valid JSON config received, using default settings'
        )
        processAudioTranscription()
      }
    } catch (e) {
      // If not valid JSON, treat as audio data
      console.log(
        '[TCP] Failed to parse JSON config, using default settings:',
        e.message
      )
      processAudioTranscription()
    }
  })

  function processAudioTranscription() {
    const wavBuffer = addWavHeader(audioBuffer)
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
          const textMessage =
            voiceflowResponse.message || 'No text response available'
          console.log('Processing audio')

          if (audioUrl.startsWith('data:audio/mp3;base64,')) {
            await handleBase64Audio(
              audioUrl,
              socket,
              textMessage,
              clientConfig.includeText
            )
          } else {
            await handleStreamingAudio(
              audioUrl,
              socket,
              textMessage,
              clientConfig.includeText
            )
          }
        } catch (error) {
          console.error('Error processing Voiceflow response:', error)
          socket.write(
            JSON.stringify({ error: 'Error getting AI response' }) + '\n'
          )
          socket.end()
        }

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
  }
})

tcpServer.on('error', (err) => {
  console.error('TCP server error:', err)
})

tcpServer.listen(TCP_PORT, () => {
  console.log(`TCP server listening on port ${TCP_PORT}`)
})

// ---------------------------
// Audio Processing Functions
// ---------------------------
function addWavHeader(pcmData) {
  const numChannels = 1
  const sampleRate = 16000
  const bitsPerSample = 16
  const byteRate = (sampleRate * numChannels * bitsPerSample) / 8
  const blockAlign = (numChannels * bitsPerSample) / 8
  const subChunk2Size = pcmData.length
  const chunkSize = 36 + subChunk2Size

  const header = Buffer.alloc(44)

  header.write('RIFF', 0)
  header.writeUInt32LE(chunkSize, 4)
  header.write('WAVE', 8)
  header.write('fmt ', 12)
  header.writeUInt32LE(16, 16)
  header.writeUInt16LE(1, 20)
  header.writeUInt16LE(numChannels, 22)
  header.writeUInt32LE(sampleRate, 24)
  header.writeUInt32LE(byteRate, 28)
  header.writeUInt16LE(blockAlign, 32)
  header.writeUInt16LE(bitsPerSample, 34)
  header.write('data', 36)
  header.writeUInt32LE(subChunk2Size, 40)

  return Buffer.concat([header, pcmData])
}

function validateAudioBuffer(buffer) {
  if (buffer.length < 1024) {
    console.log('[Audio] Buffer too small for speech detection')
    return false
  }

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

  if (maxAmplitude < 1000 || averageEnergy < 100) {
    console.log('[Audio] Audio levels too low for speech detection')
    return false
  }

  return true
}

// ---------------------------
// API Functions
// ---------------------------
async function transcribeAudio(audioWavBuffer) {
  if (USE_GROQ) {
    // --- Groq Transcription ---
    try {
      // 1. Encode WAV buffer to MP3 buffer using node-lame
      console.log(
        `Encoding ${audioWavBuffer.length} bytes WAV to MP3 for Groq...`
      )
      const encoder = new Lame({
        output: 'buffer',
        raw: true, // Input is raw PCM
        sfreq: 16, // Input sample rate 16kHz
        bitwidth: 16, // Input bitwidth 16
        mode: 'm', // Input mode mono
        // MP3 Output settings
        abr: 32, // Average Bit Rate (e.g., 32 kbps - good for voice)
        // quality: 5       // LAME quality preset (0-9, 0=best, 9=fastest)
      }).setBuffer(audioWavBuffer)

      await encoder.encode()
      const mp3Buffer = encoder.getBuffer()
      console.log(`Encoded MP3 size: ${mp3Buffer.length} bytes`)

      // 2. Send MP3 buffer to Groq using axios and FormData (proven to work)
      console.log(`Sending ${mp3Buffer.length} bytes MP3 to Groq via axios...`)
      const form = new FormData()
      const mp3Stream = Readable.from(mp3Buffer)

      form.append('file', mp3Stream, {
        filename: 'audio.mp3',
        contentType: 'audio/mpeg',
        knownLength: mp3Buffer.length,
      })
      form.append('model', GROQ_WHISPER_MODEL)
      if (GROQ_WHISPER_LANGUAGE) {
        form.append('language', GROQ_WHISPER_LANGUAGE)
      }

      const response = await axios.post(GROQ_API_URL, form, {
        headers: {
          ...form.getHeaders(), // Sets Content-Type: multipart/form-data; boundary=...
          Authorization: `Bearer ${GROQ_API_KEY}`,
          Accept: 'application/json', // Optional, but good practice
        },
        maxContentLength: Infinity, // Allow large requests/responses
        maxBodyLength: Infinity,
      })

      // Assuming the response structure is similar to OpenAI's Whisper API
      if (!response.data || typeof response.data.text !== 'string') {
        console.log(
          'No transcription text found in Groq response:',
          response.data
        )
        throw new Error('Invalid response structure from Groq API')
      }

      if (response.data.text === '') {
        console.log('No speech detected in the audio by Groq')
        return 'I could not detect any speech in the audio. Could you please try speaking again?'
      }

      console.log('Groq transcription successful via axios.')
      return response.data.text
    } catch (error) {
      // Use the axios error logging structure
      console.error('Error during Groq transcription (axios):', error.message)
      if (error.response) {
        console.error('Groq API Error Response (axios):', {
          status: error.response.status,
          statusText: error.response.statusText,
          headers: error.response.headers,
          data: error.response.data,
        })
      } else if (error.request) {
        console.error('Groq API Request Error (axios): No response received')
      }
      throw new Error('Failed to transcribe audio using Groq')
    }
  } else {
    // --- Local Whisper Transcription (Existing Logic) ---
    console.log(
      `Sending ${audioWavBuffer.length} bytes to local Whisper ASR...`
    )
    const form = new FormData()
    const audioStream = new Readable()
    audioStream.push(audioWavBuffer)
    audioStream.push(null)

    form.append('audio_file', audioStream, {
      filename: 'audio.wav',
      contentType: 'audio/wav',
      knownLength: audioWavBuffer.length,
    })

    try {
      const response = await axios.post(
        // Note: Removed language=fr, encode=true from previous attempts, revert if needed
        `${WHISPER_SERVER_URL}/asr?task=transcribe&encode=false&vad_filter=${WHISPER_VAD_FILTER}&output=json`,
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

      if (!response.data) {
        throw new Error('No response data received from local ASR API')
      }

      if (response.data.text === '') {
        console.log('No speech detected in the audio by local Whisper')
        return 'I could not detect any speech in the audio. Could you please try speaking again?'
      }

      return response.data.text
    } catch (error) {
      if (error.response) {
        console.error('Local ASR API Error Response:', {
          status: error.response.status,
          statusText: error.response.statusText,
          data: error.response.data,
        })
        throw new Error('Failed to transcribe audio: Server error')
      } else if (error.request) {
        console.error('Local ASR API Request Error:', error.message)
        throw new Error('Failed to transcribe audio: Network error')
      } else {
        console.error('Local ASR API Error:', error.message)
        throw error
      }
    }
  }
}

async function getVoiceflowResponse(transcribedText) {
  try {
    const response = await axios.post(
      'https://general-runtime.voiceflow.com/state/user/nano2025/interact',
      {
        action: {
          type: 'text',
          payload: transcribedText,
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

    const responseData = response.data[0]?.payload

    if (!responseData) {
      throw new Error('No payload in Voiceflow response')
    }
    console.log('Voiceflow message:', responseData.message)
    return {
      message: responseData.message || '',
      audioUri: responseData.audio?.src || '',
    }
  } catch (error) {
    console.error('Error calling Voiceflow API:', error)
    throw error
  }
}

// ---------------------------
// Audio Response Handlers
// ---------------------------
async function handleBase64Audio(audioUrl, socket, message, includeText) {
  try {
    // Send the text message first if includeText is true
    if (includeText) {
      const messagePacket =
        JSON.stringify({
          type: 'text',
          message: message,
        }) + '\n'
      socket.write(messagePacket)
    }

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

    await decoder.decode()
    const pcmBuffer = decoder.getBuffer()

    // Convert to 16-bit samples
    const samples = []
    // Assuming the decoded buffer is 44.1kHz, 16-bit mono based on typical MP3s
    const inputSampleRate = 44100
    const outputSampleRate = 16000
    for (let i = 0; i < pcmBuffer.length; i += 2) {
      samples.push(pcmBuffer.readInt16LE(i))
    }

    // Downsample to 16kHz using linear interpolation with playback rate adjustment
    const adjustedSamples = []
    const ratio = (inputSampleRate / outputSampleRate) * TTS_PLAYBACK_RATE // Apply playback rate adjustment
    const outputLength = Math.floor(samples.length / ratio)

    for (let i = 0; i < outputLength; i++) {
      const inputIndex = i * ratio
      const indexBefore = Math.floor(inputIndex)
      const indexAfter = Math.min(indexBefore + 1, samples.length - 1) // Ensure we don't go out of bounds
      const fraction = inputIndex - indexBefore

      const sampleBefore = samples[indexBefore]
      const sampleAfter = samples[indexAfter]

      // Linear interpolation
      const interpolatedSample = Math.round(
        sampleBefore * (1 - fraction) + sampleAfter * fraction
      )
      adjustedSamples.push(
        Math.max(-32768, Math.min(32767, interpolatedSample)) // Clamp to 16-bit range
      )
    }

    console.log(
      `Resampled audio from ${samples.length} samples (${inputSampleRate}Hz) to ${adjustedSamples.length} samples (${outputSampleRate}Hz) with playback rate ${TTS_PLAYBACK_RATE}`
    )

    const adjustedBuffer = Buffer.alloc(adjustedSamples.length * 2)
    adjustedSamples.forEach((sample, index) => {
      adjustedBuffer.writeInt16LE(sample, index * 2)
    })

    const maxSize = 1920000 // Increased to ~4x the original size (about 60 seconds at 16kHz)
    const audioData = adjustedBuffer.slice(
      0,
      Math.min(adjustedBuffer.length, maxSize)
    )

    // Add a small delay to ensure the text message is processed first
    if (includeText) {
      await new Promise((resolve) => setTimeout(resolve, 500))
    }

    const pcmStream = new Readable()
    pcmStream.push(audioData)
    pcmStream.push(null)

    console.log('Streaming adjusted PCM audio to client...')

    setupStreamHandlers(pcmStream, socket)
    pcmStream.pipe(socket)
  } catch (error) {
    console.error('Error processing base64 audio:', error)
    socket.write(JSON.stringify({ error: 'Error converting audio' }) + '\n')
    socket.end()
  }
}

async function handleStreamingAudio(audioUrl, socket, message, includeText) {
  try {
    // Send the text message first if includeText is true
    if (includeText) {
      const messagePacket =
        JSON.stringify({
          type: 'text',
          message: message,
        }) + '\n'
      socket.write(messagePacket)
    }

    const audioResponse = await axios({
      method: 'get',
      url: audioUrl,
      responseType: 'stream',
      maxContentLength: Infinity, // Allow for larger audio files
      timeout: 60000, // 60 second timeout
    })

    console.log('Streaming audio to client...')
    setupStreamHandlers(audioResponse.data, socket)

    // Use a larger internal buffer for streaming
    const streamOpts = { highWaterMark: 32768 } // 32KB buffer
    audioResponse.data.pipe(socket, streamOpts)
  } catch (error) {
    console.error('Error fetching audio stream:', error)
    socket.write(JSON.stringify({ error: 'Error fetching audio' }) + '\n')
    socket.end()
  }
}

function setupStreamHandlers(stream, socket) {
  socket.on('error', (error) => {
    if (error.code === 'EPIPE') {
      console.log('Client disconnected before stream finished')
    } else {
      console.error('Socket error:', error)
    }
    stream.destroy()
    socket.end()
  })

  stream.on('error', (error) => {
    console.error('Stream error:', error)
    socket.end()
  })

  stream.on('end', () => {
    console.log('Audio streaming ended.')
    socket.end()
  })
}

function dataUriToBuffer(dataUri) {
  const base64Data = dataUri.split(',')[1]
  return Buffer.from(base64Data, 'base64')
}
