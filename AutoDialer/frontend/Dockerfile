# Use the official Bun image
FROM oven/bun:latest

# Set the working directory
WORKDIR /app

# Copy package.json and bun.lockb
COPY package.json bun.lockb ./

# Install dependencies
RUN bun install

# Copy the rest of the application code
COPY . .

# Build the React app
RUN bun run build

# Expose the port the app runs on
EXPOSE 8080

# Start the app
CMD ["bun", "run", "dev"]