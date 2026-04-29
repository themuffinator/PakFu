#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>

namespace {
void print_env(const char* name) {
	const char* value = std::getenv(name);
	std::cout << name << '=' << (value ? value : "") << '\n';
}

std::string first_json_string_after(const std::string& payload, const std::string& key) {
	const std::size_t key_pos = payload.find(key);
	if (key_pos == std::string::npos) {
		return {};
	}
	const std::size_t colon = payload.find(':', key_pos + key.size());
	if (colon == std::string::npos) {
		return {};
	}
	const std::size_t open = payload.find('"', colon + 1);
	if (open == std::string::npos) {
		return {};
	}
	std::string out;
	bool escaped = false;
	for (std::size_t i = open + 1; i < payload.size(); ++i) {
		const char c = payload[i];
		if (escaped) {
			out.push_back(c);
			escaped = false;
			continue;
		}
		if (c == '\\') {
			escaped = true;
			continue;
		}
		if (c == '"') {
			return out;
		}
		out.push_back(c);
	}
	return {};
}
}  // namespace

int main() {
	const std::string payload((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());

	std::cout << "PakFu metadata dump example\n";
	std::cout << "Payload bytes: " << payload.size() << '\n';
	std::cout << "Schema: " << first_json_string_after(payload, "\"schema\"") << '\n';
	std::cout << "Archive path: " << first_json_string_after(payload, "\"path\"") << '\n';
	std::cout << "First archive entry: " << first_json_string_after(payload, "\"archive_name\"") << '\n';
	print_env("PAKFU_EXTENSION_PLUGIN_ID");
	print_env("PAKFU_EXTENSION_COMMAND_ID");
	print_env("PAKFU_EXTENSION_COMMAND_CAPABILITIES");

	return payload.empty() ? 1 : 0;
}

