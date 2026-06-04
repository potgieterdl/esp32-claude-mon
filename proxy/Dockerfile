# Lightweight: distroless-ish official slim Node, no build step, no deps.
FROM node:22-alpine

WORKDIR /app

# Only package.json + sources — there are no npm dependencies to install.
COPY package.json ./
COPY server.js claude.js ./

ENV NODE_ENV=production
EXPOSE 8080

# Run as the built-in non-root user.
USER node

CMD ["node", "server.js"]
