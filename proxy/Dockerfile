# Lightweight: distroless-ish official slim Node, no build step, no deps.
FROM node:22-alpine

WORKDIR /app

# Only package.json + sources — there are no npm dependencies to install.
# (get_token.js is a local-only helper, not needed in the container.)
COPY package.json ./
COPY server.js claude.js claude_oauth.js ./

# Writable dir for the live, auto-refreshing OAuth token store (option b).
# Owned by the non-root user; back it with a volume (see docker-compose.yml) to
# persist the rotating refresh token across restarts.
RUN mkdir -p /data && chown node:node /data
ENV OAUTH_STORE_FILE=/data/oauth_token.json

ENV NODE_ENV=production
EXPOSE 8080

# Run as the built-in non-root user.
USER node

CMD ["node", "server.js"]
