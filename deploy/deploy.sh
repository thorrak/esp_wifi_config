#!/bin/bash
set -e

LOGFILE="$HOME/deploy.log"
APP_DIR="$HOME/app"

echo "=== Deploy started at $(date) ===" >> "$LOGFILE"

cd "$APP_DIR"

echo "Pulling latest code..." >> "$LOGFILE"
git pull >> "$LOGFILE" 2>&1

echo "Installing dependencies..." >> "$LOGFILE"
cd website
npm install >> "$LOGFILE" 2>&1

echo "Building site..." >> "$LOGFILE"
npm run build >> "$LOGFILE" 2>&1

echo "=== Deploy finished at $(date) ===" >> "$LOGFILE"
