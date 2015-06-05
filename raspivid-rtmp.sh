set -x
raspivid -n -mm matrix -w 1280 -h 720 -fps 25 -g 50 -t 0 -b 500000 -o - | ffmpeg -f h264 -r 25 -i - -c:v copy -an -f flv -rtmp_buffer 100 -rtmp_live live rtmp://localhost/rtmp/webcam
