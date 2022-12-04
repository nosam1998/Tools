import os
import sys


class Merger:
    def __init__(self) -> None:
        self.current_directory = os.getcwd()
        # Added "_merged" just in case you're merging a file in the same directory called ".gitignore"
        self.output_filename = os.path.join(self.current_directory, ".gitignore_merged")
        self.input_files = self.input_files_abs_path(sys.argv[1:])
        self.all_lines = []
        self.already_seen = set()
        self.ignore_line = {"#"}
        self.driver()

    def input_files_abs_path(self, paths):
        return [os.path.join(self.current_directory, path) for path in paths]

    def line_breaks(self, lines):
        temp_list = []
        temp_set = set()
        for line in lines:
            print(f"{len(line)}: {line}")
            if len(line) == 0:
                if len(temp_set) > 0:
                    temp_list.append(temp_set)
                    temp_set = set()
            elif line not in self.already_seen:
                self.all_lines
                self.already_seen.add(line)
            else:
                self.already_seen.add(line)
        return temp_list

    def reader(self, abs_filepath: str):
        with open(abs_filepath, 'r') as f:
            for line in f:
                if line not in self.already_seen:
                    self.all_lines.append(line)
                    self.already_seen.add(line)

    def read_all_files(self):
        for filepath in self.input_files:
            self.reader(filepath)
            
    def writer(self, abs_filepath, data):
        with open(abs_filepath, 'w') as f:
            for line in data:
                f.write(line)

    def driver(self):
        self.read_all_files()
        for l in self.all_lines:
            print(l)
        self.writer(self.output_filename, self.all_lines)

m = Merger()
