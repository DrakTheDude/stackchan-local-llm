# xiaozhi-esp32-server, with the Chinese that reaches the model or the user
# translated. See english-pass.py for what and why.
#
# 🔴 A DERIVED IMAGE RATHER THAN BIND MOUNTS. Mounting patched .py files over
#    the originals was the obvious cheaper option and it is the wrong one: the
#    mount would keep applying after an upstream bump moved the code underneath
#    it, and connection.py is 77KB of file we would then be pinning wholesale to
#    a stale copy. Patching at build time touches only the exact strings, and
#    fails the build if they are gone.
#
# 🔴 PINNED BY DIGEST, NOT BY TAG. server_latest is a MOVING tag: the same
#    string resolves to a different server whenever upstream pushes, so a
#    rebuild months from now would quietly swap the base out from under a set of
#    patches written against this exact source. A digest cannot be re-pointed.
#
#    This is the digest that was actually running when the english pass was
#    written and verified - captured from the local image, not looked up - so
#    the patches and the base are known to match.
#
#    TO UPGRADE, deliberately: pull the new server_latest, read its digest with
#      docker image inspect ... --format '{{json .RepoDigests}}'
#    put it here, and rebuild. If upstream has moved any of the patched strings
#    the build FAILS and names them, which is the point - that is the moment to
#    re-check, not six weeks later when the robot says 再见.
FROM ghcr.io/xinnan-tech/xiaozhi-esp32-server@sha256:012859cafdac63fe3e99779525191ba420f9d80da74c1c65321b0a62075172bd

COPY patches/english-pass.py /tmp/english-pass.py
RUN python3 /tmp/english-pass.py && rm /tmp/english-pass.py

# Non-language upstream bugs, kept separate so english-pass.py stays one job.
# Same assert-or-fail discipline - see the header of that file.
COPY patches/upstream-fixes.py /tmp/upstream-fixes.py
RUN python3 /tmp/upstream-fixes.py && rm /tmp/upstream-fixes.py
# 🎭 A body's persona and voice. The firmware sends which body it is wearing in
#    the handshake headers; this makes the server act on it. Separate from the
#    two above because it is a FEATURE rather than a translation or a bug fix.
COPY patches/bodies.py /tmp/bodies.py
RUN python3 /tmp/bodies.py && rm /tmp/bodies.py
