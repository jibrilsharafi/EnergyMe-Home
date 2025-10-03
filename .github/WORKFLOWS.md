# GitHub Actions Workflows

This document describes the GitHub Actions workflows configured for this repository.

## Workflows

### 1. PlatformIO Build (`platformio-build.yml`)

Builds the firmware for ESP32 using PlatformIO.

**Triggers:**
- **Automatic**: Runs on push or pull request to `main`, `dev`, or `prod` branches
- **Manual**: Can be triggered manually from GitHub Actions UI using the "Run workflow" button

**What it does:**
- Builds firmware for two environments: `esp32dev` and `esp32dev_nosecrets`
- Creates dummy secrets for build (required files)
- Uploads the `esp32dev_nosecrets` firmware as an artifact for testing
- Uses caching to speed up builds

**Matrix strategy:** Runs parallel builds for both environments

### 2. TODO to Issue (`todo-to-issue.yml`)

Automatically creates GitHub issues from TODO comments in the code.

**Triggers:**
- **Automatic**: Runs on push to `main` or `dev` branches
- **Manual**: Can be triggered manually from GitHub Actions UI using the "Run workflow" button

**What it does:**
- Scans code for TODO comments
- Creates GitHub issues for each TODO found
- Requires `issues: write` permission

## How to Manually Run Workflows

1. Go to the **Actions** tab in the GitHub repository
2. Select the workflow you want to run from the left sidebar
3. Click the **"Run workflow"** button (appears only if `workflow_dispatch` is configured)
4. Select the branch to run on
5. Click the green **"Run workflow"** button to start

## Branch Strategy

The workflows are configured to run on the following branches:
- **main**: Primary development branch
- **dev**: Development branch (if used)
- **prod**: Production release branch (if used)

## Troubleshooting

### Workflow not appearing in GitHub Actions
- Ensure the workflow file is in `.github/workflows/` directory
- Verify the YAML syntax is valid
- The workflow must be on the default branch or the branch you're viewing to appear in the UI

### Workflow not triggering on push
- Check that you're pushing to one of the configured branches (`main`, `dev`, or `prod`)
- Ensure the workflow file is present in the branch you're pushing to
- Check the workflow run history for any errors

### Cannot manually run workflow
- Ensure `workflow_dispatch:` is present in the `on:` section of the workflow
- You must have write access to the repository
- The workflow file must be on the branch you're trying to run it from
