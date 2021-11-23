#!/bin/bash
cat << EOF | unix2dos
X-Header: true

.hola
..hola
.
EOF
cat > /dev/null
