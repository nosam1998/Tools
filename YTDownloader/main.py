from tkinter import Tk, TclError
from pytube import YouTube
import time


class YTDownloader:
    def __init__(self, download_dir="downloads/"):
        self.download_dir = download_dir
        self.tkroot = Tk()
        self.yt_url_check = "youtube.com/watch?v="
        self.current_url = ""
        self.time_to_sleep = 1

        self.main()

    def download_from_url(self, url: str) -> bool:
        YT = YouTube(url, use_oauth=True, allow_oauth_cache=True)
        YT.streams.filter(progressive=True)
        ys = YT.streams.get_highest_resolution()
        ys.download(self.download_dir)
        print(f"Downloaded {YT.title} successfully to {self.download_dir}!")

    def is_url_in_clipboard(self):
        self.tkroot.withdraw()
        clipboard = self.tkroot.clipboard_get()

        if self.yt_url_check in clipboard and clipboard != self.current_url:
            self.current_url = clipboard
            return True

        return False

    def main(self) -> None:
        while True:
            try:
                if self.is_url_in_clipboard():
                    self.download_from_url(self.current_url)
                time.sleep(self.time_to_sleep)
            except TclError:
                time.sleep(self.time_to_sleep)


ytd = YTDownloader()
