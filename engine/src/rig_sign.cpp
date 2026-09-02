// rig-sign: signs a mandate the way a user's device would.
//
// Prints the two headers /api/admit needs. Use --forge to sign with a throwaway key
// instead of the enrolled one, which is how you reproduce the "different device"
// attack by hand rather than trusting a button.
#include "rig/crypto.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
      "usage: rig-sign <mandate.json> [--forge] [--key wal/device.key]\n"
      "  default: signs with the ENROLLED device key (accepted)\n"
      "  --forge: signs with a throwaway key (refused at admission)\n");
    return 2;
  }
  std::string path = argv[1], key_path = "wal/device.key";
  bool forge = false;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--forge") forge = true;
    else if (a == "--key" && i + 1 < argc) key_path = argv[++i];
  }
  std::ifstream f(path, std::ios::binary);
  if (!f) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return 2; }
  std::ostringstream b; b << f.rdbuf();
  const std::string body = b.str();

  // The signature covers the EXACT bytes that will be sent as the request body.
  // Re-serialising the JSON would change them and invalidate the signature.
  if (forge) {
    rig::UserDevice attacker("attacker_device");
    const auto sig = attacker.sign(body.data(), body.size());
    const auto pub = attacker.public_key();
    std::printf("X-Device-Pubkey: %s\n", rig::hex(pub).c_str());
    std::printf("X-Device-Signature: %s\n", rig::hex(sig).c_str());
    std::fprintf(stderr, "\n  signed with a THROWAWAY key (%s) -- admission will refuse this\n\n",
                 rig::hex(pub.data(), 8).c_str());
  } else {
    rig::UserDevice phone("user_phone", key_path);
    const auto sig = phone.sign(body.data(), body.size());
    const auto pub = phone.public_key();
    std::printf("X-Device-Pubkey: %s\n", rig::hex(pub).c_str());
    std::printf("X-Device-Signature: %s\n", rig::hex(sig).c_str());
    std::fprintf(stderr, "\n  signed with the enrolled device (%s)\n\n",
                 rig::hex(pub.data(), 8).c_str());
  }
  return 0;
}
