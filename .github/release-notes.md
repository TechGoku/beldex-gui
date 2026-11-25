Native desktop wallet for **Beldex (BDX)**, built with Qt 6 / QML and Beldex core's wallet library linked in-process (no wallet-rpc, no Electron).

### Downloads

| System | File |
|--------|------|
| Windows 10/11 (64-bit) | `beldex-wallet-*-windows-x86_64.exe` (installer) or `.zip` (portable) |
| macOS Apple Silicon (M1+) | `beldex-wallet-*-darwin-arm64.dmg` |
| macOS Intel | `beldex-wallet-*-darwin-x86_64.dmg` |
| Linux x86_64, any distro | `beldex-wallet-*-linux-x86_64.AppImage` (`chmod +x`, then run) |
| Debian / Ubuntu | `beldex-wallet_*_amd64.deb` / `_arm64.deb` |
| Fedora / openSUSE | `beldex-wallet-*.x86_64.rpm` / `.aarch64.rpm` |
| Linux ARM64 | `beldex-wallet-*-linux-arm64.AppImage` |

Each package includes the wallet and `beldexd`, which the wallet can run as a local node. Verify your download against `SHA256SUMS.txt`.

### What's in it

- Create, restore (seed or keys) and open wallets on mainnet, testnet and devnet
- Send (several recipients, Flash), receive (accounts, subaddresses, QR codes), history with CSV export, contacts
- Master node staking and unlocking; Beldex Name Service: register, renew, update
- Nodes: Auto (a public node, otherwise a local beldexd), Remote, or Local. Public nodes for mainnet and testnet
- Switch a wallet between mainnet and testnet from the network badge: the same seed, kept as a separate wallet per network
- Live sync progress; the app stays usable and closes quickly during long scans

### Please note

- **Pre-release.** Sending, staking and BNS haven't been tested with real funds yet. Try them with small amounts or on testnet first.
- **Unsigned builds.** On Windows, SmartScreen asks you to confirm ("More info" → "Run anyway"). On macOS, right-click the app → Open the first time.
- Built on Beldex core's `feature-privacy-token` branch, which is what the testnet (v22) runs; it isn't a Beldex core release yet. The first switch of a wallet to testnet downloads the testnet block list, which takes several minutes. Devnet can't start in current core.
