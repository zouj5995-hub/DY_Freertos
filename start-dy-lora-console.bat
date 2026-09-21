@echo off
setlocal
cd /d "%~dp0"
start "DY LoRa Console" http://localhost:8000/dy-lora-console.html
python -m http.server 8000
