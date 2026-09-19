# espeak-ng behind an OpenAI-compatible speech endpoint.
#
# Deliberately tiny: espeak-ng, Python, and app.py. No framework, no wheels, no
# build step - the whole image is a few tens of megabytes and starts instantly,
# which matters because it is an OPTIONAL voice that most people will never run.
#
# 🔑 The point of it is not to be small, though. For a period body it is the
#    period-correct synthesiser: formant synthesis, the same class of thing a
#    System 7 machine spoke with. A neural voice imitating a robot is an
#    impression of one.
FROM debian:bookworm-slim

# --no-install-recommends keeps this from dragging in an audio stack it will
# never use: espeak-ng writes a WAV to stdout and never opens a sound device.
RUN apt-get update \
    && apt-get install -y --no-install-recommends espeak-ng python3 \
    && rm -rf /var/lib/apt/lists/*

COPY app.py /app/app.py

# Nothing here needs root, and a container that runs a subprocess on text it was
# handed over the network is exactly the one to drop privileges in.
RUN useradd --system --create-home speaker
USER speaker

EXPOSE 8890
CMD ["python3", "/app/app.py"]
