#!/bin/bash
cat << EOF | unix2dos
X-Version: $POP3FILTER_VERSION
X-User: $POP3_USERNAME
X-Origin: $POP3_SERVER

Body.
EOF
cat > /dev/null
