const dgram = require('dgram')
const server = dgram.createSocket('udp4')

let packetCount = 0
const startTime = Date.now()
let audioBuffer = Buffer.alloc(0)
let lastBufferSize = 0

server.on('error', (err) => {
  console.log(`Server error:\n${err.stack}`)
  server.close()
})

server.on('message', (msg, rinfo) => {
  packetCount++

  // Only log every 100th packet to avoid console spam
  const shouldLog = packetCount % 100 === 0

  if (shouldLog) {
    console.log(
      `\n[Packet #${packetCount}] From ${rinfo.address}:${rinfo.port}`
    )
    console.log(`Packet size: ${msg.length} bytes`)
  }

  // VBAN packet processing
  if (msg.length >= 28) {
    try {
      const vbanStr = msg.slice(0, 4).toString()

      // Only process if it's a valid VBAN packet
      if (vbanStr === 'VBAN') {
        const sampleRate = msg.readUInt32LE(4)
        const samplesPerFrame = msg.readUInt8(8)
        const channels = msg.readUInt8(9)
        const dataFormat = msg.readUInt8(10)
        const streamName = msg.slice(16, 24).toString().replace(/\0/g, '')

        if (shouldLog) {
          console.log('VBAN Header Analysis:')
          console.log(`  Sample Rate: ${sampleRate} Hz`)
          console.log(`  Samples per Frame: ${samplesPerFrame}`)
          console.log(`  Channels: ${channels}`)
          console.log(`  Data Format: ${dataFormat}`)
          console.log(`  Stream Name: "${streamName}"`)
        }

        // Extract audio payload
        const payload = msg.slice(28)
        audioBuffer = Buffer.concat([audioBuffer, payload])

        if (shouldLog) {
          console.log(`  Total audio buffer size: ${audioBuffer.length} bytes`)
        }
      } else if (shouldLog) {
        console.log(`Invalid VBAN header: "${vbanStr}"`)
      }
    } catch (e) {
      console.log('Error processing VBAN packet:', e.message)
    }
  } else if (shouldLog) {
    console.log('Non-VBAN packet received')
    console.log('Raw data:', msg.toString())
  }

  // Print statistics every 5 seconds
  if (Date.now() - startTime > 5000 && audioBuffer.length !== lastBufferSize) {
    const runningTime = (Date.now() - startTime) / 1000
    console.log(`\nStatistics after ${runningTime.toFixed(1)} seconds:`)
    console.log(`Total packets: ${packetCount}`)
    console.log(
      `Packet rate: ${(packetCount / runningTime).toFixed(1)} packets/sec`
    )
    console.log(`Audio buffer size: ${audioBuffer.length} bytes`)
    lastBufferSize = audioBuffer.length
  }
})

server.on('listening', () => {
  const address = server.address()
  console.log(`\nVBAN Test Server (Enhanced)`)
  console.log(`Listening on ${address.address}:${address.port}`)
  console.log(`Node.js ${process.version}`)
  console.log(`Platform: ${process.platform}`)
  console.log('\nWaiting for VBAN packets...\n')
})

// Bind to all interfaces
server.bind(6980, '0.0.0.0')

// Handle graceful shutdown
process.on('SIGINT', () => {
  console.log('\nShutting down...')
  const runningTime = (Date.now() - startTime) / 1000
  console.log(`\nFinal Statistics:`)
  console.log(`Run time: ${runningTime.toFixed(1)} seconds`)
  console.log(`Total packets: ${packetCount}`)
  console.log(
    `Average packet rate: ${(packetCount / runningTime).toFixed(1)} packets/sec`
  )
  console.log(`Final audio buffer size: ${audioBuffer.length} bytes`)
  server.close(() => process.exit(0))
})
