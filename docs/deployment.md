# Deployment

The recommended deployment is the supplied multi-stage Docker image. Create `.env` from `.env.example`, supply the Discord token/application ID, and run `docker compose up -d --build`. Mount `/data` persistently and include the database in protected backups.

The image compiles and tests before producing the runtime stage, installs timezone data and CA certificates, runs without root privileges, and receives SIGTERM for clean shutdown. Do not bake `.env` into an image or publish it.

For source deployments, install a C++20 compiler, CMake, SQLite3/Cairo development packages, OpenSSL, zlib, pkg-config, and Git. Run the standard configure/build/test commands from the README. Use a service manager such as systemd with restart limits and a dedicated unprivileged account.

GitHub Actions verifies source changes but GitHub does not run the bot continuously. A real VPS, container host, home server, or trusted platform is required.

