const dgram = require('dgram')
const server = dgram.createSocket('udp4')

let packetCount = 0
const startTime = Date.now()
let audioBuffer = Buffer.alloc(0)
let lastBufferSize = 0
let lastStatsTime = Date.now()

// Print startup banner
console.log('\n=== VBAN Test Server (Enhanced) ===')
console.log('Node.js Version:', process.version)
console.log('Platform:', process.platform)
console.log('Process ID:', process.pid)

server.on('error', (err) => {
  console.error(`\nServer error:\n${err.stack}`)
  server.close()
})

server.on('message', (msg, rinfo) => {
  packetCount++
  const now = Date.now()

  // Always log the first 5 packets, then every 100th
  const shouldLog = packetCount <= 5 || packetCount % 100 === 0

  console.log(`\nPacket #${packetCount} from ${rinfo.address}:${rinfo.port}`)
  console.log(`Size: ${msg.length} bytes`)

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
        const protocolVersion = msg.readUInt8(11)
        const streamName = msg.slice(16, 24).toString().replace(/\0/g, '')
        const frameCounter = msg.readUInt32LE(24)

        console.log('VBAN Header Analysis:')
        console.log(`  Magic String: "${vbanStr}"`)
        console.log(`  Sample Rate: ${sampleRate} Hz`)
        console.log(`  Samples/Frame: ${samplesPerFrame}`)
        console.log(`  Channels: ${channels}`)
        console.log(`  Data Format: ${dataFormat}`)
        console.log(`  Protocol Ver: ${protocolVersion}`)
        console.log(`  Stream Name: "${streamName}"`)
        console.log(`  Frame Counter: ${frameCounter}`)

        // Extract audio payload
        const payload = msg.slice(28)
        audioBuffer = Buffer.concat([audioBuffer, payload])
        console.log(`  Payload size: ${payload.length} bytes`)
        console.log(`  Total buffer: ${audioBuffer.length} bytes`)
      } else {
        console.log(`Warning: Invalid VBAN header: "${vbanStr}"`)
      }
    } catch (e) {
      console.error('Error processing VBAN packet:', e)
    }
  } else {
    console.log('Warning: Packet too small to be VBAN')
    try {
      console.log('Raw data:', msg.toString())
    } catch (e) {
      console.log('Could not convert packet to string:', e.message)
    }
  }

  // Print statistics every 5 seconds
  if (now - lastStatsTime >= 5000) {
    const runningTime = (now - startTime) / 1000
    console.log('\n=== Statistics Update ===')
    console.log(`Running time: ${runningTime.toFixed(1)} seconds`)
    console.log(`Total packets: ${packetCount}`)
    console.log(`Packet rate: ${(packetCount / runningTime).toFixed(1)}/sec`)
    console.log(`Buffer size: ${audioBuffer.length} bytes`)
    lastStatsTime = now
  }
})

server.on('listening', () => {
  const address = server.address()
  console.log('\nServer Details:')
  console.log(`Local Address: ${address.address}`)
  console.log(`Port: ${address.port}`)
  console.log(`Family: ${address.family}`)
  console.log('\nWaiting for VBAN packets...\n')
})

// Bind to all interfaces
server.bind(6980, '0.0.0.0')

// Handle graceful shutdown
process.on('SIGINT', () => {
  const runningTime = (Date.now() - startTime) / 1000
  console.log('\n\nShutting down...')
  console.log('\nFinal Statistics:')
  console.log(`Run time: ${runningTime.toFixed(1)} seconds`)
  console.log(`Total packets: ${packetCount}`)
  console.log(`Average rate: ${(packetCount / runningTime).toFixed(1)}/sec`)
  console.log(`Final buffer: ${audioBuffer.length} bytes`)
  server.close(() => process.exit(0))
})
