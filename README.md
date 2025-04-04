# Tico Nano UDP Audio Server

A Node.js server that receives raw PCM audio data over UDP, processes it through Whisper for transcription, and returns Voiceflow TTS responses.

## Features

- Receives 16-bit PCM audio data over UDP (port 6980)
- Processes audio with noise gate and gain adjustment
- Transcribes audio using integrated Whisper ASR service
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
- Whisper ASR Service: Integrated transcription service running on port 9000
- API Integration: Voiceflow Dialog Manager
- Audio Response: MP3 decoding and PCM streaming

## Whisper ASR Service

The project includes an integrated Whisper ASR service using the [openai-whisper-asr-webservice](https://github.com/ahmetoner/whisper-asr-webservice) Docker image. The service is configured to:

- Use the base model
- Run on CPU
- Cache models to improve startup time
- Support multiple output formats (text, JSON, VTT, SRT, TSV)
- Provide word-level timestamps
- Filter out non-speech audio with voice activity detection (VAD)

## License

MIT
