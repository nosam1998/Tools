# YTDownloader

Automatically download YouTube videos by only copying the link.

## Steps

1. Install Python 3.10 (If you don't have it already)
2. Install the requirements using `pip install -r requirements.txt`. You might need to prefix that command with `python3 -m`, like so `python3 -m pip install -r requirements.txt`
3. Run the script `python3 main.py`!
4. Go to YouTube and copy a YouTube link. The download should start almost immediately.

## Possible Features and Ideas

- [ ] Activate/Run on keyboard shortcut instead of a while loop (The way this is currently done is using a while loop and that's not good...)
- [ ] Add resolution thresholds (minimum acceptable resolution and/or maximum acceptable resolution)
- [ ] Add ffmpeg support to get the best quality video (Currently the only way to do this is to download the video and audio seperately and splice them together)
