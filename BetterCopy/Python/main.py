import argparse
import glob
import os
import shutil
import ntpath
from pathlib import Path


class FileTree:
    def __init__(self, base_path: str, depth: int, include_patterns: list[str], exclude_patterns: list[str]) -> None:
        self.base_path = base_path
        self.include_patterns = include_patterns
        self.exclude_patterns = exclude_patterns
        self.folders = []
        self.files = []
        self.depth = depth

        self.ls()

    def abs_path(self, rel_path) -> str:
        return os.path.join(self.base_path, rel_path)

    def pattern_matcher(self, s) -> bool:
        # in_include_patterns = all(not s.match(x) for x in self.include_patterns)
        # in_exclude_patterns = all(not s.match(x) for x in self.exclude_patterns)
        return all(not s.match(x) for x in self.exclude_patterns)
        # We return it this way so that in_include_patterns takes precedence.
        # return True if in_include_patterns else in_exclude_patterns

    def str_to_path(self, s):
        if type(s) == str:
            return Path(s)
        else:
            print(f"Error happened! {s}")
            exit(1)

    def get_filename(self, original_path, path):
        ntpath.basename(original_path)
        head, tail = ntpath.split(path)
        return tail or ntpath.basename(head)

    def abs_to_rpath_helper(self, original_path, curr_path, is_file=False):
        if is_file:
            return self.get_filename(original_path, curr_path)
        else:
            return str(curr_path).replace(original_path, "")

    def pretty_print_file_helper(self, folder, files, depth, original_base_path):
        print(f"{'  ' * depth}| {self.abs_to_rpath_helper(original_base_path, folder)}")
        for f in files:
            print(f"{'  ' * depth}    - {self.abs_to_rpath_helper(original_base_path, f, is_file=True)}")

    def pretty_print_file_structure(self, original_base_path):
        q = self.folders[:]

        while q:
            temp_folder = q.pop()
            q.extend(temp_folder.folders)
            pretty_path = self.abs_to_rpath_helper(original_base_path, temp_folder.base_path)
            self.pretty_print_file_helper(pretty_path, temp_folder.files, temp_folder.depth, original_base_path)

    def ls(self):
        path_obj = self.str_to_path(self.base_path)
        if not path_obj.is_dir():
            self.print_error_with_verbosity("Please provide a valid dir!")
            return None

        for p in path_obj.iterdir():
            if self.pattern_matcher(p):
                if p.is_file():
                    self.files.append(p)
                elif p.is_dir():
                    f = FileTree(p, self.depth + 1, self.include_patterns, self.exclude_patterns)
                    self.folders.append(f)
                else:
                    self.print_error_with_verbosity(f"Unaccepted file/folder type at: {p}")



# From: https://stackoverflow.com/a/287944/9297141
class Helpers:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

    def print_with_verbosity(self, msg, verbose, curr_verbosity) -> None:
        if verbose <= curr_verbosity:
            print(f"{msg}")
    
    def print_info_with_verbosity(self, msg, verbose, curr_verbosity) -> None:
        if verbose <= curr_verbosity:
            print(f"{self.OKCYAN}{msg}{self.ENDC}")

    def print_warning_with_verbosity(self, msg, verbose, curr_verbosity) -> None:
        if verbose <= curr_verbosity:
            print(f"{self.WARNING}{msg}{self.ENDC}")

    def print_error_with_verbosity(self, msg, curr_verbosity) -> None:
        # Errors should ALWAYS print! Regardless of verbosity...
        if curr_verbosity > 0:
            print(f"{self.FAIL}{msg}{self.ENDC}")

    def file_to_list(self, filepath) -> list[str]:
        with open(filepath, "r") as f:
            return [line.strip() for line in f if "#" not in line]

    def build_folder_structure(self, src, include_patterns, exclude_patterns, max_depth=None) -> list[FileTree]:
        done = False
        curr_depth = 0
        root = FileTree(src, 0, include_patterns, exclude_patterns)
        q = [root]
        next_q = []
        s = {
            0: []
        }

        while not done:
            if len(q) > 0:
                temp = q.pop()
            
                if not max_depth:
                    f = FileTree(temp, curr_depth, include_patterns, exclude_patterns)
                    next_q.append(f)
                    s[curr_depth].append(f)
                elif curr_depth <= max_depth:
                    f = FileTree(temp, curr_depth, include_patterns, exclude_patterns)
                    next_q.append(f)
                    s[curr_depth].append(f)
                else:
                    done = True
            else:
                s[len(s)] = []
                q = next_q[:]
                next_q = []
                curr_depth += 1

            
        return s

    def do_deep_copy(self, root: FileTree, dst: str, follow_symlinks=False):
        q = [root]
        original_path = root.base_path

        if os.path.exists(dst):
            self.print_error_with_verbosity(f"Path at {dst} already exists! Please use the -e/--allow-empty option to use an already existing directory!")
            exit(1)

        os.mkdir(dst)

        while q:
            n = q.pop()
            temp_dir_output_path = os.path.join(dst, n.abs_to_rpath_helper(original_path, n.base_path))
            os.mkdir(temp_dir_output_path)
            q.extend(n.folders)
            for f in n.files:
                temp_src = os.path.join(n.base_path, f)
                temp_dst = os.path.join(temp_dir_output_path, f)
                shutil.copy(temp_src, temp_dst, follow_symlinks=follow_symlinks)


class BetterCopy(Helpers):
    def __init__(self) -> None:
        self.verbose = None
        self.quiet = None
        self.ignore_file = None
        self.ignore_patterns = []
        self.include_file = None
        self.include_patterns = []
        self.follow_symlinks = False
        self.src = None
        self.dst = None
        self.max_depth = None
        self.testing_mode = False
        self.file_tree = dict()
        # Parse the args
        self.init()

    def init(self):
        parser = argparse.ArgumentParser()
        
        group = parser.add_mutually_exclusive_group()
        # 0: Display nothing
        # 1: Display Errors (Default)
        # 2: Display Errors, and Warnings
        # 3: Display Errors, Warnings, and Info
        group.add_argument("-v", "--verbose", type=int, choices=[0, 1, 2, 3], help="0: Display nothing ... 3: Display everything", default=1)
        group.add_argument("-q", "--quiet", action="store_true")

        parser.add_argument("-g", "--use-gitignore", help="Use the .gitignore file in the src directory instead of .bcignore. (NOT tested!)", action="store_true")
        parser.add_argument("-l", "--follow-symlinks", help="Follow symlinks when copying files.", action="store_true")
        parser.add_argument("--ignore-file", type=str, help="Ignore file. The default is: .bcignore", default=".bcignore")
        parser.add_argument("--include-file", type=str, help="Source directory The default is: .bcinclude", default=".bcinclude")
        parser.add_argument("-d", "--max-depth", type=int, help="The max level of folders you want to traverse down")
        parser.add_argument("-t", "--testing", help="Print out what will happen and don't copy.", action="store_true")
        
        parser.add_argument("src", type=str, help="Source directory")
        parser.add_argument("dst", type=str, help="Destination directory")
 
        args = parser.parse_args()

        self.verbose = args.verbose

        if args.quiet:
            self.verbose = 0
        
        if args.use_gitignore:
            args.src = os.path.join(args.src, ".gitignore")

        self.src = Path(args.src)
        if not self.src.is_dir():
            self.print_error_with_verbosity(f"The path src \"{self.src}\" is not a directory!")
            exit(1)

        self.dst = Path(args.dst)
        # if not self.dst.is_dir():
        #     self.print_error_with_verbosity(f"The path for dst \"{self.dst}\" is not a directory!", self.verbose)
        #     exit(1)

        self.ignore_file = Path(os.path.join(args.src, args.ignore_file))
        if not self.ignore_file.is_file():
            self.print_error_with_verbosity(f"The path for --ignore-file \"{self.ignore_file}\" is not a file!", self.verbose)
            exit(1)

        self.include_file = Path(os.path.join(args.src, args.include_file))
        if not self.ignore_file.is_file():
            self.print_error_with_verbosity(f"The path for --include-file \"{self.include_file}\" is not a file!", self.verbose)
            exit(1)

        if args.max_depth:
            self.max_depth = args.max_depth

        if args.testing:
            self.testing_mode = True

        self.read_pattern_files()

        if len(self.ignore_patterns) == 0 and len(self.include_patterns) == 0:
            self.print_error_with_verbosity("Please check that you've setup your .bcinclude and .bcignore files correctly.", self.verbose)

        if args.follow_symlinks:
            self.follow_symlinks = True

        if self.testing_mode:
            self.testing()
        else:
            # Run the main program
            self.runner()

    def read_pattern_files(self):
        self.ignore_patterns = self.file_to_list(self.ignore_file)
        # self.include_patterns = self.file_to_list(self.include_file)
        
        self.print_info_with_verbosity(f"{self.ignore_file}: {self.ignore_patterns}", 3, self.verbose)
        # self.print_info_with_verbosity(f"{self.include_file}: {self.include_patterns}", 3, self.verbose)

    def testing(self):
        if self.verbose != 3:
            self.print_warning_with_verbosity(f"We recommend setting your verbosity to 3 so you can debug easier. It's currently {self.verbose}", 0, self.verbose)

        self.print_info_with_verbosity("Testing mode enabled!", 3, self.verbose)
        f = FileTree(str(self.src.resolve()), 0, self.include_patterns, self.ignore_patterns)
        f.pretty_print_file_structure(f.base_path)

    def runner(self):
        self.print_info_with_verbosity(f"src: {self.src.resolve()}\ndst: {self.dst.resolve()}\nIgnore file path: {self.ignore_file.resolve()}\nInclude file path: {self.include_file.resolve()}", 3, self.verbose)
        self.print_info_with_verbosity(f"src: {self.src}\ndst: {self.dst}\nIgnore file path: {self.ignore_file}\nInclude file path: {self.include_file}", 3, self.verbose)

        if not self.max_depth:
            self.file_tree = self.build_folder_structure(self.src, self.include_patterns, self.ignore_patterns)
        else:
            self.file_tree = self.build_folder_structure(self.src, self.include_patterns, self.ignore_patterns, max_depth=self.max_depth)

        f = FileTree(str(self.src.resolve()), 0, self.include_patterns, self.ignore_patterns)

        self.do_deep_copy(f, self.dst.resolve(), self.follow_symlinks)

if __name__ == "__main__":
    bc = BetterCopy()
