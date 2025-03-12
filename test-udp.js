const dgram = require('dgram')
const server = dgram.createSocket('udp4')

let packetCount = 0
const startTime = Date.now()

server.on('error', (err) => {
  console.log(`Server error:\n${err.stack}`)
  server.close()
})

server.on('message', (msg, rinfo) => {
  packetCount++
  console.log(`\n[Packet #${packetCount}] From ${rinfo.address}:${rinfo.port}`)
  console.log(`Packet size: ${msg.length} bytes`)

  // Detailed packet inspection
  if (msg.length >= 28) {
    // VBAN header size
    try {
      // VBAN header analysis
      const vbanStr = msg.slice(0, 4).toString()
      const sampleRate = msg.readUInt32LE(4)
      const samplesPerFrame = msg.readUInt8(8)
      const channels = msg.readUInt8(9)
      const dataFormat = msg.readUInt8(10)
      const streamName = msg.slice(16, 24).toString().replace(/\0/g, '')

      console.log('VBAN Header Analysis:')
      console.log(`  Magic String: "${vbanStr}"`)
      console.log(`  Sample Rate: ${sampleRate} Hz`)
      console.log(`  Samples per Frame: ${samplesPerFrame}`)
      console.log(`  Channels: ${channels}`)
      console.log(`  Data Format: ${dataFormat}`)
      console.log(`  Stream Name: "${streamName}"`)

      // Payload size check
      const payloadSize = msg.length - 28
      console.log(`  Payload size: ${payloadSize} bytes`)

      // First few bytes of payload (if any)
      if (payloadSize > 0) {
        const payloadPreview = msg.slice(28, Math.min(38, msg.length))
        console.log('  Payload preview (first 10 bytes):', payloadPreview)
      }
    } catch (e) {
      console.log('Error parsing VBAN header:', e.message)
    }
  } else {
    console.log('Packet too small to be VBAN (should be at least 28 bytes)')
    console.log('Raw data:', msg)
  }

  // Print statistics every 100 packets or 10 seconds
  if (packetCount % 100 === 0 || Date.now() - startTime > 10000) {
    const runningTime = (Date.now() - startTime) / 1000
    console.log(`\nStatistics after ${runningTime.toFixed(1)} seconds:`)
    console.log(`Total packets received: ${packetCount}`)
    console.log(
      `Packet rate: ${(packetCount / runningTime).toFixed(1)} packets/sec`
    )
  }
})

server.on('listening', () => {
  const address = server.address()
  console.log(`\nVBAN Test Server`)
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
  console.log(`Total running time: ${runningTime.toFixed(1)} seconds`)
  console.log(`Total packets received: ${packetCount}`)
  console.log(
    `Average packet rate: ${(packetCount / runningTime).toFixed(1)} packets/sec`
  )
  server.close(() => process.exit(0))
})
