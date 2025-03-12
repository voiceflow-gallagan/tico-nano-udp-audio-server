const dgram = require('dgram')
const server = dgram.createSocket('udp4')

console.log('\n=== Echo Nano UDP Test Server ===')
console.log('Listening for any UDP packets on port 6980...\n')

server.on('message', (msg, rinfo) => {
  console.log(
    `\nReceived ${msg.length} bytes from ${rinfo.address}:${rinfo.port}`
  )

  // Try to interpret as VBAN
  if (msg.length >= 28) {
    try {
      const header = {
        magic: msg.slice(0, 4).toString(),
        sampleRate: msg.readUInt32LE(4),
        samplesPerFrame: msg.readUInt8(8),
        channels: msg.readUInt8(9),
        dataFormat: msg.readUInt8(10),
        streamName: msg.slice(16, 24).toString().replace(/\0/g, ''),
      }

      console.log('Packet Analysis:')
      console.log(JSON.stringify(header, null, 2))

      // Show first few bytes of payload
      const payload = msg.slice(28)
      console.log(`Payload (${payload.length} bytes):`)
      console.log('First 16 bytes:', payload.slice(0, 16))
    } catch (e) {
      console.log('Error parsing packet:', e.message)
      console.log('Raw data:', msg)
    }
  } else {
    console.log('Packet too small for VBAN header')
    console.log('Raw data:', msg.toString())
  }
})

server.on('listening', () => {
  const address = server.address()
  console.log(`Server Details:`)
  console.log(`- Address: ${address.address}`)
  console.log(`- Port: ${address.port}`)
  console.log(`- Family: ${address.family}`)
})

server.on('error', (err) => {
  console.error('Server error:', err)
})

server.bind(6980, '0.0.0.0')
