#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <ranges>
#include <string>

#include <boost/program_options.hpp>

#include <TDirectoryFile.h>
#include <TFile.h>
#include <TH1D.h>

struct config {
  std::string input_file;
  std::string output_file;
};

config parse_options(int argc, char **argv) {
  namespace po = boost::program_options;
  config cfg;

  po::options_description desc("Allowed options");
  // clang-format off
  desc.add_options()
      ("help", "produce help message")
      ("input,i", po::value<std::string>(&cfg.input_file)->required(), "input file (required)")
      ("output,o", po::value<std::string>(&cfg.output_file)->default_value("out.txt"), "output file (default: out.txt)");
  // clang-format on

  po::variables_map vm;
  try {
    po::store(po::parse_command_line(argc, argv, desc), vm);
    if (vm.count("help")) {
      std::cout << desc << "\n";
      exit(0);
    }
    po::notify(vm);
  } catch (const po::error &e) {
    std::cerr << "Error parsing command line options: " << e.what() << "\n";
    std::cerr << desc << "\n";
    exit(1);
  }

  return cfg;
}

std::set<std::string> lookup_all_objs(TDirectoryFile *curr_dir,
                                         const std::string &dir_name = "/") {
  std::set<std::string> ret{};
  for (auto key : *(curr_dir->GetListOfKeys())) {
    auto obj = curr_dir->Get(key->GetName());
    if (obj->IsA() == TDirectoryFile::Class()) {
      auto sub_dir = dynamic_cast<TDirectoryFile *>(obj);
      auto sub_dir_name = dir_name + key->GetName() + "/";
      auto sub_objs = lookup_all_objs(sub_dir, sub_dir_name);
      ret.insert(sub_objs.begin(), sub_objs.end());
    } else if (obj->IsA() == TH1D::Class()) {
      auto full_name = dir_name + key->GetName();
      ret.insert(full_name);
    }
  }
  return ret;
}

int main(int argc, char **argv) {
  auto [input, output] = parse_options(argc, argv);
  std::unique_ptr<TFile> input_root(TFile::Open(input.c_str(), "READ"));
  if (!input_root || input_root->IsZombie()) {
    std::cerr << "Error opening input file: " << input << "\n";
    return 1;
  }
  auto objects =
      lookup_all_objs(dynamic_cast<TDirectoryFile *>(input_root.get()));

  auto output_file = std::ofstream(output, std::ios::out | std::ios::trunc);
  for (const auto &obj_name : objects) {
    auto obj = input_root->Get<TH1D>(obj_name.c_str());
    auto nbins = obj->GetNbinsX();
    for (int bin{}; bin < nbins; bin++) {
      output_file << obj_name << '#' << bin << '\n';
    }
  }
  output_file.close();
  return 0;
}
