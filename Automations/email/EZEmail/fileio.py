import json
import os
import time
from imbox.vendors.gmail import GmailMessages
from imbox.messages import Messages


class FileType:
    # For backwards compatibility
    TXT = "txt"
    JSON = "json"

    def __init__(self, supported_file_types: set = {"txt", "json"}):
        self.supported_file_types = supported_file_types

    def is_supported(self, file_type: str) -> bool:
        if file_type in self.supported_file_types:
            return True

        return False

    def add_supported_file_type(self, file_type: str = "") -> None:
        self.supported_file_types.add(file_type)


class FileIO:
    def __init__(
            self, base_path: str = "", filename: str = "", mode: str = "w+"
    ) -> None:
        self.base_path = base_path
        if not base_path:
            env_abs_path = os.environ.get("EZEMAIL_EMAILS_DIR")
            if env_abs_path:
                self.base_path = env_abs_path
            else:
                self.base_path = os.getcwd()

        self.filename = self.generate_filename(filename)
        self.mode = mode

        self.filepath_raw = os.path.join(self.base_path, os.environ.get("EZEMAIL_RAW_EMAILS_DIR"))
        self.filepath_clean = os.path.join(self.base_path, os.environ.get("EZEMAIL_CLEAN_EMAILS_DIR"))

    def generate_filename(self, filename: str) -> str:
        if not filename:
            localtime = time.strftime('%c').lower()
            temp_name = "_".join(localtime.split(" "))
            filename = f"file-{temp_name}.txt"

        return os.path.join(self.base_path, filename)

    def abs_filepath_from_email(self, email: GmailMessages | Messages, raw: bool = False,
                                file_ext: str = FileType.TXT) -> str:
        if not FileType.is_supported(file_ext):
            exit(1)

        temp_filename = f"{email.subject}.{file_ext}"

        if not raw:
            return os.path.join(self.filepath_clean, temp_filename)

        return os.path.join(self.filepath_raw, temp_filename)

    def write_email(self, email: GmailMessages | Messages) -> None:
        raw_email_filepath = self.abs_filepath_from_email(email, raw=True, file_ext=FileType.TXT)
        with open(raw_email_filepath, self.mode) as f:
            f.write(json.dumps(obj=repr(email), sort_keys=True, allow_nan=True, indent=4))

        clean_email_filepath = self.abs_filepath_from_email(email, raw=False, file_ext=FileType.TXT)
        with open(clean_email_filepath, self.mode) as f:
            temp = f"Subject: {email.subject}\nBody: {email.body}"
            f.write(temp)

    def write(self, text: str) -> None:
        with open(self.filename, self.mode) as f:
            f.write(text)

    def read_text(self) -> list[str]:
        with open(self.filename, self.mode) as f:
            return [line for line in f.readlines()]
