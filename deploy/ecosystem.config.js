module.exports = {
  apps: [
    {
      name: "webhook",
      script: "./webhook.js",
      cwd: "/home/configwifi",
      env: {
        WEBHOOK_SECRET: "CHANGE_ME",
      },
    },
  ],
};
