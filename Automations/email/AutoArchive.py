import os
import json
import logging
import logging
logging.basicConfig(filename='email_archival_automation.log', filemode='a', format='%(asctime)s - %(levelname)s - %(message)s', datefmt='%d-%b-%y %H:%M:%S')

from EZEmail.ez_email import EZEmail
import plac
from datetime import datetime, timedelta

# Load env variables
from dotenv import load_dotenv

load_dotenv()


@plac.flg('dryrun', abbrev='r', help='Won\'t change anything, but instead will print out the first 5 actions that '
                                     'would be taken. Good for testing.')
@plac.opt('days', abbrev='d', help='Number of days before archiving the email. For example \"10\"', type=int)
@plac.opt('weeks', abbrev='w', help='Number of weeks before archiving the email. For example \"2\"', type=int)
def main(days=0, weeks=1, dryrun=False):
    # The size of the batch to use
    batch_size = 100
    # Total time delay in days
    total_days = 0
    if days > 0:
        total_days += days

    if weeks > 0:
        total_days += weeks * 7

    if dryrun:
        logging.warning(msg="Doing dryrun! (If you want to commit the changes then remote the --dryrun/-r flag.)")

    archival_date = datetime.now() - timedelta(days=total_days)
    archived_counter = 0
    with EZEmail() as e:
        start, end = None, None
        b_start, b_end = None, None
        for uid, message in e.get_emails_on_specific_date(archival_date, folder="inbox"):
            start = int(uid)
            b_start = uid
            break

        logging.info(msg=f"Got starting uid {start}")


        for uid, message in e.get_emails_on_specific_date(archival_date-timedelta(weeks=5), folder="inbox"):
            end = int(uid)
            b_end = uid
            break

        logging.info(msg=f"Got end uid {end}")

        logging.info(f"s {start} e {end}")
        curr = start
        if start - end > batch_size:
            while curr >= end and curr > 0:
                curr -= batch_size

                for uid, message in e.get_emails_with_uid_gt(curr):
                    if not dryrun:
                        # Move from the inbox (selected by default) to all mail (all_mail == archived)
                        e.imbox.move(uid, "all_mail")

                    archived_counter += 1
                    logging.info(msg=f"Archived: uid: {uid} subject: {message.subject}")

        for uid, message in e.get_emails_with_uid_gt(end):
            if not dryrun:
                # Move from the inbox (selected by default) to all mail (all_mail == archived)
                e.imbox.move(uid, "all_mail")

            archived_counter += 1
            logging.info(msg=f"Archived: uid: {uid} subject: {message.subject}")

    logging.info(msg=f"Archived {archived_counter} total emails!")


if __name__ == "__main__":
    plac.call(main)
