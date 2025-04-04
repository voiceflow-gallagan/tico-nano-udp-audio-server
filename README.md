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

The project includes a `.env.template` file that you can use as a starting point. Copy it to create your own `.env` file:

```bash
cp .env.template .env
```

Then edit the `.env` file with your specific values:

```env
# Voiceflow API Key
VF_DM_API_KEY=your_voiceflow_api_key

# Port Configuration
UDP_PORT=6980  # Optional, defaults to 6980
TCP_PORT=12345 # Optional, defaults to 12345

# Whisper ASR Settings
WHISPER_MODEL=base
WHISPER_ENGINE=openai_whisper
WHISPER_DEVICE=cpu
WHISPER_VAD_FILTER=false
```

### Whisper ASR Configuration Options

#### Available Engines

- `openai_whisper`: The original OpenAI Whisper implementation
- `faster_whisper`: Optimized implementation with improved performance
- `whisperx`: Enhanced version with speaker diarization support

#### Available Models

**Standard Models:**
- `tiny`, `base`, `small`, `medium`
- `large-v1`, `large-v2`, `large-v3` (or `large`)
- `large-v3-turbo` (or `turbo`)

**English-Optimized Models:**
- `tiny.en`, `base.en`, `small.en`, `medium.en`

**Distilled Models:**
- `distil-large-v2`, `distil-medium.en`, `distil-small.en`, `distil-large-v3`
  (Only available for whisperx and faster-whisper engines)

**Model Selection Tips:**
- For English-only applications, the `.en` models tend to perform better, especially for `tiny.en` and `base.en`
- The difference becomes less significant for `small.en` and `medium.en` models
- Distilled models offer improved inference speed while maintaining good accuracy
- For production use, consider using `faster_whisper` with `distil-medium.en` for a good balance of speed and accuracy

#### Device Options

- `cpu`: Run on CPU (default)
- `cuda`: Run on GPU (requires NVIDIA GPU with CUDA support)

#### Voice Activity Detection (VAD)

- `WHISPER_VAD_FILTER=true`: Enable VAD filtering to remove non-speech segments with Faster Whisper engine
- `WHISPER_VAD_FILTER=false`: Disable VAD filtering (default)

VAD filtering helps improve transcription accuracy by removing background noise and silence. It's particularly useful for:
- Reducing false positives in transcription
- Improving processing speed by focusing only on speech segments
- Enhancing the quality of transcriptions in noisy environments

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

3. Set up your environment variables:
```bash
cp .env.template .env
# Edit .env with your specific values
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
- Whisper ASR Service: Integrated transcription service
- API Integration: Voiceflow Dialog Manager
- Audio Response: MP3 decoding and PCM streaming

## Whisper ASR Service

The project includes an integrated Whisper ASR service using the [openai-whisper-asr-webservice](https://github.com/ahmetoner/whisper-asr-webservice) Docker image. The service is configured to:

- Use configurable models and engines via environment variables
- Run on CPU or GPU (if available)
- Cache models to improve startup time
- Support multiple output formats (text, JSON, VTT, SRT, TSV)
- Provide word-level timestamps
- Filter out non-speech audio with voice activity detection (VAD)

### Performance Considerations

- For CPU-only environments, use smaller models like `tiny.en` or `base.en`
- For better performance, use the `faster_whisper` engine
- For production use with English content, consider `distil-medium.en` with `faster_whisper`
- For multi-language support, use standard models like `medium` or `large-v3`
- Enable VAD filtering to improve transcription quality and reduce processing time

## License

MIT
