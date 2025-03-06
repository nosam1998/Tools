import os

from .fileio import FileIO
from imbox import Imbox
from datetime import datetime
import re


class GmailQueryBuilder:
    def __init__(self, sep: str = " "):
        self.query = [""]
        self.sep = sep

    def build(self) -> str:
        return f"{self.sep}".join(self.query)


class EZEmail:
    def __init__(self) -> None:
        """
        host: str -> 1.1.1.1 OR smtp.google.com
        port: int | None -> 993 | None
        username: str -> johndoe123
        password: str -> securepassword123
        """
        self.host = os.environ.get("EZEMAIL_HOST")
        self.port = os.environ.get("EZEMAIL_PORT", 993)
        self.username = os.environ.get("EZEMAIL_USERNAME")
        self.password = os.environ.get("EZEMAIL_PASSWORD")

        self.imbox = Imbox(
            hostname=self.host,
            username=self.username,
            password=self.password,
            ssl=True,
            ssl_context=None,
            starttls=False,
        )

        # self.main()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.imbox.logout()

    def is_valid_email(self, email: str) -> bool:
        if re.fullmatch(r"[^@]+@[^@]+\.[^@]+", email):
            return True

        return False

    def get_all_folders(self):
        return self.imbox.folders()

    def get_all_emails_from_inbox(self):
        return self.imbox.messages()

    def get_all_emails_unread(self):
        return self.imbox.messages(unread=True)

    def get_all_emails_by_flag(self, is_flagged: bool = False):
        if is_flagged:
            return self.imbox.messages(flagged=True)
        else:
            return self.imbox.messages(unflagged=True)

    def get_emails_from_today(self):
        return self.get_emails_after_date(datetime.now())

    def get_emails_on_specific_date(self, dt: datetime, **kwargs):
        return self.imbox.messages(date__on=dt, **kwargs)

    def get_emails_in_date_range(self, start: datetime, end: datetime):
        return self.imbox.messages(date__lt=start, date__gt=end)

    def get_emails_before_date(self, dt: datetime, **kwargs):
        return self.imbox.messages(date__lt=dt, **kwargs)

    def get_emails_after_date(self, dt: datetime):
        return self.imbox.messages(date__gt=dt)

    def get_email_by_uid(self, uid: int = 1):
        return self.imbox.messages(uid__range=f"{uid}:{uid}")

    def get_emails_with_uid_in_range(self, uid_range: str = "105:*"):
        return self.imbox.messages(uid__range=uid_range)

    def get_emails_by_contains_subject(self, subject: str = "contains this text"):
        return self.imbox.messages(subject=subject)

    def get_emails_with_uid_lt(self, uid_lt: int = 100):
        return self.get_emails_with_uid_in_range(f"0:{uid_lt}")

    def get_emails_with_uid_gt(self, uid_gt: int = 100):
        return self.get_emails_with_uid_in_range(f"{uid_gt}:*")

    def get_emails_by_folder(self, folder_name: str = "Social"):
        return self.imbox.messages(folder=folder_name)

    def get_emails_by_sender(self, sender: str):
        return self.imbox.messages(sent_from=f"{sender}")

    def get_emails_by_receiver(self, receiver: str):
        return self.imbox.messages(sent_to=f"{receiver}")

    def get_emails_by_sender_and_has_attachment(self, sender: str):
        return self.imbox.messages(folder='all', raw=f'from:{sender} has:attachment')

    def get_emails_by_label(self, label: str):
        return self.imbox.messages(folder='all', label=label)

    def get_emails_by_raw_query_str(self, query: str):
        return self.imbox.messages(folder='all', raw=query)

    def emails_to_list(self, data):
        return [{"uid": uid, "subject": message.subject, "message": message} for uid, message in data]

    def main(self):
        for uid, message in self.get_emails_from_today():
            FileIO(filename=f"{message.subject}.json").write_email(message)
        self.imbox.logout()
