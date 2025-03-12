const dgram = require('dgram')
const client = dgram.createSocket('udp4')

// Create a simple VBAN header
function createVBANHeader() {
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

  // Frame counter (0 for this test)
  header.writeUInt32LE(0, 24)

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

// Combine header and audio data
const packet = Buffer.concat([createVBANHeader(), createDummyAudio()])

// Send test packet
const SERVER_IP = '5.161.85.204' // Your server IP
const SERVER_PORT = 6980

console.log('Sending VBAN test packet...')
console.log(`Packet size: ${packet.length} bytes`)
console.log('Header: VBAN + 44.1kHz + mono + int16')
console.log('Samples per frame: 64')

client.send(packet, SERVER_PORT, SERVER_IP, (err) => {
  if (err) {
    console.error('Error sending packet:', err)
  } else {
    console.log('Test packet sent successfully')
  }
  client.close()
})
