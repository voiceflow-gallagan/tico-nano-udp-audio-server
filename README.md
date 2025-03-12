# Tico Nano UDP Audio Server

A Node.js server that receives raw PCM audio data over UDP, processes it through Whisper for transcription, and returns Voiceflow TTS responses.

## Features

- Receives 16-bit PCM audio data over UDP (port 6980)
- Processes audio with noise gate and gain adjustment
- Transcribes audio using Whisper ASR
- Generates responses using Voiceflow Dialog Manager
- Returns TTS audio responses to clients

## Requirements

- Node.js v20 or later
- Docker and Docker Compose (for containerized deployment)
- `lame` package for MP3 decoding

## Environment Variables

Create a `.env` file with:

```env
VF_DM_API_KEY=your_voiceflow_api_key
WHISPER_SERVER_URL=your_whisper_server_url
UDP_PORT=6980  # Optional, defaults to 6980
TCP_PORT=12345 # Optional, defaults to 12345
```

## Installation

1. Clone the repository:
```bash
git clone https://github.com/voiceflow/tico-nano-udp-audio-server.git
cd tico-nano-udp-audio-server
```

2. Install dependencies:
```bash
npm install
```

## Running

### Local Development

```bash
node server.js
```

### Docker Deployment

```bash
docker compose up -d
```

## Architecture

- UDP Server (Port 6980): Receives raw PCM audio data
- TCP Server (Port 12345): Handles client connections for responses
- Audio Processing: Noise gate, gain adjustment, and WAV formatting
- API Integration: Whisper ASR and Voiceflow Dialog Manager
- Audio Response: MP3 decoding and PCM streaming

## License

MIT
