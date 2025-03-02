import imaplib
import csv
import email
import os
from dataclasses import dataclass
from dotenv import load_dotenv

load_dotenv()  # take environment variables from .env


@dataclass
class LoanFunded:
    borrower: str
    loan_reason: str
    payback_date: str
    loan_principal: str
    solo_donation: str
    lender_tip: str
    payback_amount: str
    status: str = "Funded"  # Default status

    def get_key(self):
        return f"{self.borrower}/{self.payback_amount}"

    def clean(self):
        self.borrower = self.borrower.strip()
        self.loan_reason = self.loan_reason.strip()
        self.payback_date = self.payback_date.strip()
        self.loan_principal = self.loan_principal.strip()
        self.solo_donation = self.solo_donation.strip()
        self.lender_tip = self.lender_tip.strip()
        self.payback_amount = self.payback_amount.strip()

        self.loan_principal = float(self.loan_principal[1:])
        self.solo_donation = float(self.solo_donation[1:])
        self.lender_tip = float(self.lender_tip[1:])
        self.payback_amount = float(self.payback_amount[1:])

    def serialize(self):
        return {
            "borrower": self.borrower,
            "loan_reason": self.loan_reason,
            "payback_date": self.payback_date,
            "loan_principal": self.loan_principal,
            "solo_donation": self.solo_donation,
            "lender_tip": self.lender_tip,
            "payback_amount": self.payback_amount,
        }


def parse_loan_funded_email(msg) -> LoanFunded:
    lines = msg.splitlines()
    details = {}

    for i in range(len(lines)):
        if lines[i] == "Borrower":
            details["borrower"] = lines[i + 1]
        elif lines[i] == "Loan Reason":
            details["loan_reason"] = lines[i + 1]
        elif lines[i] == "Payback Date":
            details["payback_date"] = lines[i + 1]
        elif lines[i] == "Loan Principal":
            details["loan_principal"] = lines[i + 1]
        elif lines[i] == "*SoLo Donation*":
            details["solo_donation"] = lines[i + 1]
        elif lines[i] == "*Lender Tip*":
            details["lender_tip"] = lines[i + 1]
        elif lines[i] == "Payback Amount":
            details["payback_amount"] = lines[i + 1]

    return LoanFunded(**details)


def parse_loan_repaid_email(msg):
    lines = msg.splitlines()
    for i in range(len(lines)):
        if "has repaid their loan for" in lines[i]:
            l = lines[i].split(" has repaid their loan for ")
            name = l[0]
            reason = l[1].split(" $")[0]
            amount = l[1].split(" $")[1].split(" ")[0]
            return f"{name}/{float(amount)}"


def write_funded_loans_to_csv(data: [LoanFunded], filename):
    with open(filename, "w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(
            [
                "Borrower",
                "Loan Reason",
                "Payback Date",
                "Loan Principal",
                "SoLo Donation",
                "Lender Tip",
                "Payback Amount",
                "Status",
            ]
        )
        for loan in data:
            writer.writerow(
                [
                    loan.borrower,
                    loan.loan_reason,
                    loan.payback_date,
                    loan.loan_principal,
                    loan.solo_donation,
                    loan.lender_tip,
                    loan.payback_amount,
                    loan.status,
                ]
            )


class GmailSearcher:
    def __init__(self, username, password, mailbox="inbox"):
        """
        Initialize the GmailSearcher with your Gmail credentials and desired mailbox.
        """
        self.username = username
        self.password = password
        self.mailbox = mailbox
        self.mail = None
        self.connect()

    def connect(self):
        """
        Connect to the Gmail IMAP server and log in.
        """
        self.mail = imaplib.IMAP4_SSL("imap.gmail.com")
        self.mail.login(self.username, self.password)
        self.mail.select(self.mailbox)

    def decode_subject(self, subject):
        """
        Decode an email subject header.
        """
        if subject is None:
            return ""
        subject_parts = email.header.decode_header(subject)
        decoded_subject = ""
        for part, encoding in subject_parts:
            if isinstance(part, bytes):
                part = part.decode(encoding or "utf-8", errors="ignore")
            decoded_subject += part
        return decoded_subject

    def search_by_subject(self, exact_subject):
        """
        Search for emails with an exact subject.

        Parameters:
            exact_subject (str): The exact subject string to search for.

        Returns:
            list: A list of email IDs that match the exact subject.
        """
        # Initial search using the SUBJECT keyword. This may return emails where the subject
        # contains the search string.
        result, data = self.mail.search(None, "SUBJECT", f'"{exact_subject}"')
        email_ids = data[0].split()
        exact_matching_ids = []

        # Iterate over the found emails and filter for an exact subject match.
        for email_id in email_ids:
            result, msg_data = self.mail.fetch(email_id, "(RFC822)")
            raw_email = msg_data[0][1]
            msg = email.message_from_bytes(raw_email)
            subject = self.decode_subject(msg["Subject"])

            if subject == exact_subject:
                exact_matching_ids.append(email_id)

        return exact_matching_ids

    def get_email_content_by_id(self, email_id):
        """
        Retrieve the email content for a given email ID.

        Parameters:
            email_id (bytes): The email ID to fetch.

        Returns:
            email.message.Message: The parsed email message object.
        """
        result, msg_data = self.mail.fetch(email_id, "(RFC822)")
        raw_email = msg_data[0][1]
        msg = email.message_from_bytes(raw_email)
        return msg

    def get_email_body(self, email_message):
        """
        Extracts and returns the plain text body from an email message.

        Args:
            email_message (email.message.EmailMessage): The email message object.

        Returns:
            str: The decoded plain text body of the email, or an empty string if not found.
        """
        body = ""

        # Check if the message is multipart
        if email_message.is_multipart():
            # Iterate over email parts
            for part in email_message.walk():
                content_type = part.get_content_type()
                content_disposition = str(part.get("Content-Disposition"))

                # Look for the plain text part and skip attachments
                if (
                    content_type == "text/plain"
                    and "attachment" not in content_disposition
                ):
                    payload = part.get_payload(decode=True)
                    if payload:
                        body = payload.decode(
                            part.get_content_charset("utf-8"), errors="replace"
                        )
                        break
        else:
            # For non-multipart messages, get the payload directly
            payload = email_message.get_payload(decode=True)
            if payload:
                body = payload.decode(
                    email_message.get_content_charset("utf-8"), errors="replace"
                )

        return body

    def logout(self):
        """
        Logout from the Gmail IMAP server.
        """
        if self.mail:
            self.mail.logout()
            self.mail = None


# Example usage:
if __name__ == "__main__":
    # You thought the code above was bad? Wait until you see this! Look further down at your own risk! LOL

    # Replace with your actual Gmail credentials
    username = os.getenv("username")
    password = os.getenv("password")  # Use an app-specific password if needed
    loan_funded_subject = "Loan funded = day made. Nice work!"
    load_repaid_subject = "Congratulations -- your SoLo loan has been repaid!"
    searcher = GmailSearcher(username, password)

    # Example: Search for emails with a specific subject.
    # subject_to_search = "Your Exact Subject Here"
    matching_email_ids = searcher.search_by_subject(loan_funded_subject)
    print("Emails with an exact matching subject:", matching_email_ids)

    # If there is at least one matching email, get its contents.
    loans = []
    if matching_email_ids:
        for i in matching_email_ids:
            email_msg = searcher.get_email_content_by_id(i)
            body = searcher.get_email_body(email_msg)
            loan = parse_loan_funded_email(body)
            loan.clean()
            loans.append(loan)

    loans_dict = {l.get_key(): l for l in loans}
    loans_paid = []
    matching_email_ids = searcher.search_by_subject(load_repaid_subject)
    print("Emails with an exact matching subject:", matching_email_ids)
    if matching_email_ids:
        for i in matching_email_ids:
            email_msg = searcher.get_email_content_by_id(i)
            body = searcher.get_email_body(email_msg)
            key = parse_loan_repaid_email(body)
            if key in loans_dict:
                loans_dict[key].status = "Paid"

    write_funded_loans_to_csv(loans_dict.values(), "funded_solo_loans.csv")
    searcher.logout()
