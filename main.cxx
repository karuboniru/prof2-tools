#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>

#include <boost/program_options.hpp>

#include <TFile.h>
#include <TH1.h>
#include <sstream>

#include "Professor/Ipol.h"
#include "Professor/ParamPoints.h"

struct config {
  std::string scan_dir;
  std::string prediction_file;
  std::string param_file;
  std::string bin_list;

  int order{};
  int n_test{};

  std::string output;

  bool include_header{true};
};

config parse_options(int argc, char **argv) {
  config cfg;
  namespace po = boost::program_options;
  po::options_description desc("Allowed options");
  // clang-format off
  desc.add_options()
      ("help", "produce help message")
      ("scan-dir,s", po::value<std::string>(&cfg.scan_dir)->required(), "directory to scan for ROOT files (required)")
      ("prediction-file,p", po::value<std::string>(&cfg.prediction_file)->default_value("prediction.root"), "file to write predictions to (default: prediction.root)")
      ("param-file,f", po::value<std::string>(&cfg.param_file)->default_value("params.dat"), "file containing parameters (default: params.dat)")
      ("bin-list,b", po::value<std::string>(&cfg.bin_list)->default_value("bin.list"), "file containing parameters (default: bin.list)")
      ("order", po::value<int>(&cfg.order)->default_value(4), "polynomial order (default: 4)")
      ("n-test", po::value<int>(&cfg.n_test)->default_value(0), "number of test points (default: 0, no test points)")
      ("output,o", po::value<std::string>(&cfg.output)->default_value("output.ipol"), "file containing parameters (default: output.ipol)")
      ("include-header", po::bool_switch(&cfg.include_header)->default_value(true), "include header in output file (default: true)")
      ;
  // clang-format on
  po::variables_map vm;
  try {
    po::store(po::parse_command_line(argc, argv, desc), vm);
    if (vm.count("help")) {
      std::cout << desc << '\n';
      exit(0);
    }
    po::notify(vm);
  } catch (const po::error &e) {
    std::cerr << "Error parsing command line options: " << e.what() << '\n';
    std::cerr << desc << '\n';
    exit(1);
  }
  if (!std::filesystem::is_directory(cfg.scan_dir)) {
    std::cerr << "Error: " << cfg.scan_dir << " is not a valid directory.\n";
    exit(1);
  }

  return cfg;
}

std::vector<double> read_params(const std::filesystem::path &param_file) {
  std::vector<double> params{};
  std::ifstream file(param_file);
  std::string line;
  while (std::getline(file, line)) {
    std::istringstream iss(line);
    std::string token;
    if (iss >> token && iss >> token) {
      params.push_back(std::stod(token));
    }
  }
  return params;
}

std::vector<std::string> read_names(const std::filesystem::path &param_file) {
  std::vector<std::string> params{};
  std::ifstream file(param_file);
  std::string line;
  while (std::getline(file, line)) {
    std::istringstream iss(line);
    std::string token;
    if (iss >> token) {
      params.emplace_back(token);
    }
  }
  return params;
}

std::vector<std::pair<std::string, int>>
read_bin_list(const std::string &file) {
  std::vector<std::pair<std::string, int>> bins;
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    size_t pos = line.find('#');
    if (pos != std::string::npos) {
      std::string text = line.substr(0, pos);
      int id = std::stoi(line.substr(pos + 1));
      bins.emplace_back(text, id);
    }
  }
  return bins;
}

Professor::Ipol
build_ipol(const Professor::ParamPoints &param_points,
           const std::vector<double> &values, int order,
           const std::string &name,
           const std::vector<std::vector<double>> &test_params = {}) {
  if (test_params.size() == 0) {
    return Professor::Ipol(param_points, values, order, name);
  }

  // the first test_params.size() of values are the test values
  // do exclude them from the interpolation
  auto used_vars = values | std::views::drop(test_params.size()) |
                   std::ranges::to<std::vector>();

  auto best_ipol = std::ranges::min(
      std::views::iota(0, order + 1) | std::views::transform([&](auto i) {
        return Professor::Ipol(param_points, used_vars, i, name);
      }) | std::views::transform([&](auto ipol) {
        auto sum_resudals = std::ranges::fold_left_first(
            std::views::zip(test_params, values) |
                std::views::transform([&](const auto &pair) {
                  const auto [params, val] = pair;
                  return std::pow(ipol.value(params) - val, 2);
                }),
            std::plus{});
        return std::make_pair(ipol, sum_resudals);
      }),
      [](const auto &a, const auto &b) { return a.second < b.second; });
  return best_ipol.first;
}

int main(int argc, char **agrv) {
  auto cfg = parse_options(argc, agrv);
  auto param_files = std::filesystem::directory_iterator(cfg.scan_dir) |
                     std::views::transform([&](auto &dir_entry) {
                       const auto &dir = dir_entry.path();
                       auto param = read_params(dir / cfg.param_file);
                       auto filename = dir / cfg.prediction_file;
                       return std::make_pair(param, filename);
                     }) |
                     std::ranges::to<std::vector>();
  std::cout << "Found " << param_files.size()
            << " parameter files in directory " << cfg.scan_dir << '\n';
  // if cfg.n_test is set, the fitst cfg.n_test points are used as test
  // so exclude them from the param_points
  Professor::ParamPoints param_points(param_files | std::views::keys |
                                      std::views::drop(cfg.n_test) |
                                      std::ranges::to<std::vector>());
  // and the test points
  auto test_params = param_files | std::views::keys |
                     std::views::take(cfg.n_test) |
                     std::ranges::to<std::vector>();

  auto file_vector =
      param_files | std::views::values | std::ranges::to<std::vector>();

  auto bin_list = read_bin_list(cfg.bin_list);
  // outer vector is for bins, inner for files, since the Ipols are built
  // for each bin and file combination
  std::vector<std::vector<double>> prediction_values{};
  prediction_values.resize(bin_list.size());
  std::ranges::for_each(prediction_values,
                        [&](auto &vec) { vec.resize(file_vector.size()); });
  // we don't want to open too many files at once, so the outer loop is
  // for file and inner for bin
  for (auto &&[file_id, file_path] : file_vector | std::views::enumerate) {
    auto file = TFile::Open(file_path.c_str(), "READ");
    if (!file || file->IsZombie()) {
      std::cerr << "Error opening file: " << file_path << "\n";
      return 1;
    }
    for (auto &&[bin_id, bin_info] : bin_list | std::views::enumerate) {
      auto [name, id] = bin_info;
      auto hist = file->Get<TH1D>(name.c_str());
      if (!hist) {
        std::println(std::cerr, "Error: Histogram {} not found in file {}",
                     name, file->GetName());
        exit(-1);
      }
      auto val = hist->GetBinContent(id + 1);
      if (std::isinf(val) || std::isnan(val)) {
        std::println(std::cerr,
                     "Error: Invalid value {} for histogram {} in file {}", val,
                     name, file->GetName());
        exit(-1);
      }
      prediction_values[bin_id][file_id] = val;
    }
    file->Close();
    delete file; // Clean up the file pointer
  }
  std::cout << "Read " << prediction_values.size() << " bins with "
            << file_vector.size() << " files.\n";
  auto result_range =
      std::views::zip(bin_list, prediction_values) |
      std::views::transform([&](const auto &bin_pred) {
        auto bin = std::get<0>(bin_pred);
        auto values = std::get<1>(bin_pred);
        auto [name, id] = bin;
        return build_ipol(param_points, values, cfg.order,
                          std::format("{}#{}", name, id), test_params);
      }) |
      std::ranges::to<std::vector>();

  std::stringstream min, max;
  for (const auto &param : param_points.ptmins()) {
    min << param << " ";
  }
  for (const auto &param : param_points.ptmaxs()) {
    max << param << " ";
  }

  std::ofstream output_file(cfg.output);
  if (cfg.include_header) {
    // following is to manupulate the output format from original prof2
    // locate a random directory in the scan_dir
    auto first_run = (*(std::filesystem::directory_iterator(cfg.scan_dir) |
                        std::views::filter([](auto &dir_entry) {
                          return dir_entry.is_directory();
                        }) |
                        std::views::take(1))
                           .begin())
                         .path();

    std::print(output_file, "ParamNames: ");
    for (auto &&name : read_names(first_run / cfg.param_file)) {
      std::print(output_file, "{} ", name);
    }
    std::println(output_file, "");
    std::println(output_file, "MinParamVals: {}", min.str());
    std::println(output_file, "MaxParamVals: {}", max.str());
    std::println(output_file, "Dimension: {}", param_points.dim());
    std::println(output_file, "---");
  }

  for (const auto &[id, ipol_str] : result_range | std::views::enumerate) {
    std::println(output_file, "{} {} {}", ipol_str.name(), id, id + 1);
    std::println(output_file, "  {} {} {}", ipol_str.toString("var"), min.str(),
                 max.str());
    std::println(output_file, "  err: {} 0 0 {} {}", ipol_str.dim(), min.str(),
                 max.str());
  }
  output_file.close();
  // std::cout << "Ipols written to " << cfg.output << '\n';
  std::println(std::cout, "Ipols written to {}", cfg.output);
  return 0;
}
