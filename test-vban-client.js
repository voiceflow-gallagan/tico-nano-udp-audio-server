const dgram = require('dgram')
const client = dgram.createSocket('udp4')

// Create a simple VBAN header
function createVBANHeader(frameCounter = 0) {
  const header = Buffer.alloc(28) // VBAN header is 28 bytes

  // 'VBAN' magic string
  header.write('VBAN', 0)

  // Sample rate (44100 Hz)
  header.writeUInt32LE(44100, 4)

  // Samples per frame (64 is a typical value)
  header.writeUInt8(64, 8)

  // Channels (1 for mono)
  header.writeUInt8(1, 9)

  // Data format (1 for int16)
  header.writeUInt8(1, 10)

  // Protocol version and format
  header.writeUInt8(0x00, 11) // Protocol version 0, PCM format

  // Stream name "test1"
  header.write('test1\0\0\0', 16) // Null-padded stream name

  // Frame counter
  header.writeUInt32LE(frameCounter, 24)

  return header
}

// Create some dummy audio data (sine wave)
function createDummyAudio() {
  const sampleCount = 64 // Match samples per frame
  const buffer = Buffer.alloc(sampleCount * 2) // 2 bytes per sample for int16
  for (let i = 0; i < sampleCount; i++) {
    const value = Math.sin(i * 0.1) * 32767 // Convert to 16-bit range
    buffer.writeInt16LE(Math.floor(value), i * 2)
  }
  return buffer
}

// Server configuration
const SERVER_IP = '5.161.85.204' // Your server IP
const SERVER_PORT = 6980
const TOTAL_PACKETS = 10
const PACKET_DELAY_MS = 100

console.log(`\nVBAN Test Client`)
console.log(`Target: ${SERVER_IP}:${SERVER_PORT}`)
console.log(
  `Sending ${TOTAL_PACKETS} test packets with ${PACKET_DELAY_MS}ms delay between packets`
)
console.log('Configuration: 44.1kHz, mono, int16, 64 samples per frame\n')

// Handle errors
client.on('error', (err) => {
  console.error('Client error:', err)
  client.close()
})

// Send packets with delay
let packetsSent = 0
let lastPacketTime = Date.now()

function sendNextPacket() {
  if (packetsSent >= TOTAL_PACKETS) {
    console.log('\nAll packets sent. Waiting 1 second before closing...')
    setTimeout(() => {
      console.log('Closing client.')
      client.close()
    }, 1000)
    return
  }

  const packet = Buffer.concat([
    createVBANHeader(packetsSent),
    createDummyAudio(),
  ])
  const now = Date.now()
  const timeSinceLastPacket = now - lastPacketTime

  console.log(
    `Sending packet #${packetsSent + 1}/${TOTAL_PACKETS} (${
      packet.length
    } bytes)`
  )

  client.send(packet, SERVER_PORT, SERVER_IP, (err) => {
    if (err) {
      console.error(`Error sending packet #${packetsSent + 1}:`, err)
      client.close()
      return
    }

    packetsSent++
    lastPacketTime = now

    // Schedule next packet
    setTimeout(sendNextPacket, PACKET_DELAY_MS)
  })
}

// Try to ping the server first using DNS lookup
const dns = require('dns')
dns.lookup(SERVER_IP, (err, address) => {
  if (err) {
    console.error('DNS lookup failed:', err)
    client.close()
    return
  }

  console.log(`DNS resolution successful: ${address}`)
  console.log('Starting packet transmission...\n')
  sendNextPacket()
})
