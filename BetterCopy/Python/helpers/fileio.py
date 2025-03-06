import argparse

# From: https://stackoverflow.com/a/287944/9297141
class BetterPrint:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

    def print_with_verbosity(self, msg, verbosity=0) -> None:
        if verbosity <= self.verbosity:
            print(f"{msg}")

    
    def print_info_with_verbosity(self, msg, verbosity=0) -> None:
        if verbosity <= self.verbosity:
            print(f"{self.OKCYAN}{msg}{self.ENDC}")


    def print_warning_with_verbosity(self, msg, verbosity=0) -> None:
        if verbosity <= self.verbosity:
            print(f"{self.WARNING}{msg}{self.ENDC}")


    def print_error_with_verbosity(self, msg) -> None:
        # Errors should ALWAYS print! Regardless of verbosity...
        print(f"{self.FAIL}{msg}{self.ENDC}")


class BetterCopy(BetterPrint):
    def __init__(self) -> None:
        self.verbosity = None
        self.quiet = None

        self.ignore_file = None
        self.include_file = None

        self.src = None
        self.dst = None

        # Parse the args
        self.init()

    def init(self):
        parser = argparse.ArgumentParser()
        
        group = parser.add_mutually_exclusive_group()
        group.add_argument("-v", "--verbose", action="store_true")
        group.add_argument("-q", "--quiet", action="store_true")

        parser.add_argument("--ignore-file", type=str, help="Ignore file")
        parser.add_argument("--include-file", type=str, help="Source directory")
        
        parser.add_argument("src", type=str, help="Source directory")
        parser.add_argument("dst", type=str, help="Destination directory")

        args = parser.parse_args()

        print(args)

    def walk(self):
        pass

    def runner(self):
        pass
