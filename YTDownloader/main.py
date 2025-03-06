from tkinter import Tk, TclError
from pytube import YouTube
import time


class YTDownloader:
    def __init__(self, download_dir="downloads/"):
        """
        Initialize the Youtube Downloader class.

        Args:
            download_dir (str, optional): The directory to save the downloaded videos. Defaults to "downloads/".
        """
        self.download_dir = download_dir
        self.tkroot = Tk()
        self.yt_url_check = "youtube.com/watch?v="
        self.current_url = ""
        self.time_to_sleep = 1

        self.main()

    def download_from_url_highest_resolution(self, YT: YouTube, url: str) -> bool:
        video_parts = {
            "video": YT.streams.filter(only_video=True).get_highest_resolution(),
            "audio": YT.streams.get_audio_only(),
        }
        print("Not implemented yet :(")
        return

    def download_from_url(self, url: str, get_highest_resolution: bool) -> None:
        """
        Downloads the video from the given URL.

        Args:
            url (str): The URL of the video to download.

        Returns:
            None

        Raises:
            ValueError: If the given URL is not a valid YouTube URL.

        """

        YT = YouTube(url, use_oauth=True, allow_oauth_cache=True)
        if get_highest_resolution:
            self.download_from_url_highest_resolution(url)
            return None

        YT.streams.filter(progressive=True)
        ys = YT.streams.get_highest_resolution()
        ys.download(self.download_dir)
        print(f"Downloaded {YT.title} successfully to {self.download_dir}!")

    def is_url_in_clipboard(self):
        """
        Checks if the URL in the system clipboard is a YouTube video URL.

        Returns:
            bool: True if the URL is a YouTube video URL, False otherwise.

        """
        self.tkroot.withdraw()
        clipboard = self.tkroot.clipboard_get()

        if self.yt_url_check in clipboard and clipboard != self.current_url:
            self.current_url = clipboard
            return True

        return False

    def main(self) -> None:
        """
        The main function of the YTDownloader class.

        This function continuously checks if the URL in the system clipboard is a YouTube video URL. If it is, it downloads the video to the specified download directory. It also handles any exceptions that may occur during the process.

        Args:
            self (YTDownloader): An instance of the YTDownloader class.

        Returns:
            None

        Raises:
            TclError: If the Tkinter library raises an error.

        """
        while True:
            try:
                if self.is_url_in_clipboard():
                    self.download_from_url(self.current_url)
                time.sleep(self.time_to_sleep)
            except TclError:
                time.sleep(self.time_to_sleep)


ytd = YTDownloader()
