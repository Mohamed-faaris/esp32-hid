import type { NextConfig } from "next";
const nextConfig: NextConfig = { transpilePackages: ["@esp32-hid/device-client"] };
export default nextConfig;
