set -x
./capture-encode -nb 300000 | ffmpeg -f h264 -r 25 -i - -c:v copy -an -f flv -rtmp_buffer 100 -rtmp_live live rtmp://localhost/rtmp/webcam
