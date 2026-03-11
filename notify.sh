#!/usr/bin/env bash

PID=$1
TOPIC=$2

if [ -z "$PID" ] || [ -z "$TOPIC" ]; then
    echo "Usage: ./notify.sh <PID> <topic>"
    exit 1
fi

echo "Monitoring PID: $PID. Notification will be sent once it is finished."
echo "Subscribe to notifications at: https://ntfy.sh/$TOPIC"

# Wait for the process to finish
while kill -0 $PID 2>/dev/null; do
    sleep 10
done

# Send the notification via ntfy.sh
curl -d "PID=$PID has finished successfully!" "https://ntfy.sh/$TOPIC"

echo "Process $PID finished. Notification sent."
