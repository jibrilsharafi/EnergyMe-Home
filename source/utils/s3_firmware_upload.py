# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 Jibril Sharafi

#!/usr/bin/env python3
"""
S3 Firmware Upload Script for EnergyMe-Home

Uploads firmware artifacts (bin, elf) to fw/{version}/ and updates fw/latest.json.
Creating IoT Jobs is handled separately by tools/ota_release.py in energyme-infra.

Usage:
    python3 utils/s3_firmware_upload.py [options]

Requirements:
    pip install boto3

AWS Authentication:
    Uses AWS SSO profiles configured in ~/.aws/config

    Setup:
    1. Configure SSO: aws configure sso
    2. Login: aws sso login --profile <profile-name>
    3. Run script: python3 utils/s3_firmware_upload.py --profile <profile-name>

    Default profile is 'default' if not specified.
"""

import sys
import re
import json
import argparse
from pathlib import Path
import boto3
from botocore.exceptions import ClientError

REGION = "eu-west-1"
BUCKET_TEMPLATE = "energymesrl-energyme-home-{env}-firmware"

# PlatformIO environments that produce a dev (unoptimised) build.
# A dev build may NOT be uploaded to the prod S3 bucket.
_PIO_DEV_ENVS = {"esp32s3-dev"}


class FirmwareUploader:
    def __init__(self, aws_env="dev", environment="esp32s3-dev", profile_name="default", package_dir: str | None = None, version_override: str | None = None):
        if aws_env not in ("dev", "prod"):
            print(f"ERROR: --aws-env must be 'dev' or 'prod', got {aws_env!r}")
            sys.exit(1)
        self.aws_env = aws_env
        self.bucket_name = BUCKET_TEMPLATE.format(env=aws_env)
        self.environment = environment
        self.profile_name = profile_name
        self.version_override = version_override
        self.project_root = Path(__file__).parent.parent
        # If a package_dir is provided prefer that as the build directory (useful for CI artifact folders)
        self.package_dir = Path(package_dir) if package_dir else None
        if self.package_dir and self.package_dir.exists() and self.package_dir.is_dir():
            self.build_dir = self.package_dir
        else:
            self.build_dir = self.project_root / ".pio" / "build" / environment
        self.constants_file = self.project_root / "include" / "constants.h"

        # Initialize S3 client with AWS SSO profile
        try:
            print(f"Using AWS SSO profile: {self.profile_name}")
            session = boto3.Session(profile_name=self.profile_name)
            self.s3_client = session.client('s3', region_name=REGION)
            print(f"✅ AWS S3 client initialized for region: {REGION}")

        except Exception as e:
            print(f"ERROR: Failed to initialize AWS S3 client with profile '{self.profile_name}'")
            print(f"Error details: {e}")
            print("\nPlease ensure:")
            print("1. AWS CLI is installed")
            print("2. You have configured AWS SSO: aws configure sso")
            print("3. You are logged in: aws sso login --profile {profile_name}")
            print(f"4. Profile '{self.profile_name}' exists in ~/.aws/config")
            sys.exit(1)
    
    def verify_s3_access(self):
        """Verify S3 access and bucket existence"""
        try:
            # Check if bucket exists and we have access
            self.s3_client.head_bucket(Bucket=self.bucket_name)
            print(f"✅ S3 bucket '{self.bucket_name}' is accessible")
            return True
        except ClientError as e:
            error_code = e.response['Error']['Code']
            if error_code == '404':
                print(f"ERROR: S3 bucket '{self.bucket_name}' does not exist")
            elif error_code == '403':
                print(f"ERROR: Access denied to S3 bucket '{self.bucket_name}'")
            else:
                print(f"ERROR: Failed to access S3 bucket '{self.bucket_name}': {e}")
            return False
    
    def check_version_exists(self, version):
        """Check if a version-specific folder already exists in S3
        
        Returns:
            True if the version folder exists and contains files, False otherwise
        """
        prefix = f"{self.get_s3_folder_path(version)}/"
        try:
            response = self.s3_client.list_objects_v2(
                Bucket=self.bucket_name,
                Prefix=prefix,
                MaxKeys=1
            )
            return response.get('KeyCount', 0) > 0
        except ClientError as e:
            print(f"WARN: Could not check if version exists: {e}")
            return False
    
    def parse_version_from_constants(self):
        """Parse firmware version from constants.h"""
        if not self.constants_file.exists():
            raise FileNotFoundError(f"Constants file not found: {self.constants_file}")
        
        with open(self.constants_file, 'r') as f:
            content = f.read()
        
        # Extract version components
        major_match = re.search(r'#define\s+FIRMWARE_BUILD_VERSION_MAJOR\s+"([^"]+)"', content)
        minor_match = re.search(r'#define\s+FIRMWARE_BUILD_VERSION_MINOR\s+"([^"]+)"', content)
        patch_match = re.search(r'#define\s+FIRMWARE_BUILD_VERSION_PATCH\s+"([^"]+)"', content)
        
        if not all([major_match, minor_match, patch_match]):
            raise ValueError("Could not parse version from constants.h")
        
        version = f"{major_match.group(1)}.{minor_match.group(1)}.{patch_match.group(1)}" # type: ignore
        print(f"Parsed firmware version: {version}")
        return version
    
    def check_build_files_exist(self):
        """Find build files in the build directory.

        Supports both the default PlatformIO build layout and CI-produced
        package folders that contain versioned filenames like
        'energyme_home_1.0.1.bin' and 'energyme_home_1.0.1.elf'.

        Returns:
            (ok: bool, files: dict[str, Path], detected_version: str|None)
        """
        if not self.build_dir.exists() or not self.build_dir.is_dir():
            print(f"ERROR: Build directory not found: {self.build_dir}")
            return False, {}, None

        # Candidate patterns to look for
        firmware_patterns = [r"energyme_home_(?P<ver>[0-9]+\.[0-9]+\.[0-9]+).*\.bin$", r"^firmware\.bin$"]
        elf_patterns = [r"energyme_home_(?P<ver>[0-9]+\.[0-9]+\.[0-9]+).*\.elf$", r"^firmware\.elf$"]

        found: dict[str, Path | None] = {
            'firmware.bin': None,
            'firmware.elf': None
        }

        detected_version = None

        # Iterate files in build_dir and match patterns
        for p in self.build_dir.iterdir():
            name = p.name
            # firmware bin
            for pat in firmware_patterns:
                m = re.search(pat, name)
                if m:
                    found['firmware.bin'] = p
                    if 'ver' in m.groupdict() and m.group('ver'):
                        detected_version = m.group('ver')
                    break
            # elf
            for pat in elf_patterns:
                m = re.search(pat, name)
                if m:
                    found['firmware.elf'] = p
                    if not detected_version and 'ver' in m.groupdict() and m.group('ver'):
                        detected_version = m.group('ver')
                    break

        missing = [str(self.build_dir / k) for k, v in found.items() if v is None]
        if missing:
            print("ERROR: Missing build files in build directory:")
            for file in missing:
                print(f"  - {file}")
            # Helpful hint when user likely expected PlatformIO build dir
            print(f"\nPlease run 'platformio run --environment {self.environment}' or pass the CI package folder with --package-dir")
            return False, {}, None

        return True, found, detected_version
    
    def get_versioned_filename(self, base_filename, version):
        """Generate version-specific filename (e.g., energyme_home_0.12.40.bin)"""
        name, ext = base_filename.rsplit('.', 1)
        return f"energyme_home_{version}.{ext}"
    
    def get_s3_folder_path(self, version):
        return f"fw/{version}"
    
    def upload_file_to_s3(self, local_path, s3_key):
        """Upload a file to S3"""
        try:
            print(f"Uploading {local_path.name} to s3://{self.bucket_name}/{s3_key}")
            self.s3_client.upload_file(
                str(local_path), 
                self.bucket_name, 
                s3_key,
                ExtraArgs={'ServerSideEncryption': 'AES256'}
            )
            return True
        except ClientError as e:
            print(f"ERROR uploading {local_path.name}: {e}")
            return False
        except FileNotFoundError:
            print(f"ERROR: File not found: {local_path}")
            return False

    def upload_versioned_file(self, local_path, version, base_filename):
        """Upload a file to the versioned fw/{version}/ folder."""
        versioned_filename = self.get_versioned_filename(base_filename, version)
        key = f"{self.get_s3_folder_path(version)}/{versioned_filename}"
        return self.upload_file_to_s3(local_path, key)
    
    def upload_firmware(self, dry_run=False):
        """Main upload function"""
        print("=== EnergyMe-Home Firmware S3 Upload ===")
        print(f"PlatformIO env:  {self.environment}")
        print(f"AWS env:         {self.aws_env}")
        print(f"S3 Bucket:       {self.bucket_name}")
        print(f"Build directory: {self.build_dir}")
        print()

        # Guard: dev PlatformIO builds must never reach the prod S3 bucket
        if self.aws_env == "prod" and self.environment in _PIO_DEV_ENVS:
            print(f"ERROR: PlatformIO env '{self.environment}' is a dev build and cannot be uploaded to the prod S3 bucket.")
            print(f"       Build with 'esp32s3-prod' first, or target --aws-env dev.")
            return False

        # Verify S3 access
        if not dry_run and not self.verify_s3_access():
            return False

        # Check build files first (may provide detected version when using CI package folder)
        files_exist, build_files, detected_version = self.check_build_files_exist()
        if not files_exist:
            return False

        # Priority: 1) version_override, 2) detected_version from filenames, 3) constants.h
        if self.version_override:
            version = self.version_override
            print(f"Using version override from command line: {version}")
        elif detected_version:
            version = detected_version
            print(f"Using version detected from filenames: {version}")
        else:
            try:
                version = self.parse_version_from_constants()
            except Exception as e:
                print(f"ERROR parsing version: {e}")
                return False
        
        # Check if version already exists in S3 and ask for confirmation
        if not dry_run:
            version_exists = self.check_version_exists(version)
            if version_exists:
                print(f"⚠️  WARNING: Version {version} already exists in S3!")
                print(f"   Folder: s3://{self.bucket_name}/{self.get_s3_folder_path(version)}/")
                response = input("\nDo you want to overwrite the existing version? [y/N]: ").strip().lower()
                if response not in ['y', 'yes']:
                    print("Upload cancelled by user")
                    return False
                print()
        
        # Display file sizes
        print("Build files to upload:")
        total_size = 0
        # Sanity check: ensure none are None (static analyzers may warn otherwise)
        for name, path in build_files.items():
            if path is None:
                print(f"ERROR: unexpected missing file: {name}")
                return False
            size = path.stat().st_size
            total_size += size
            print(f"  - {name}: {size:,} bytes ({size/1024/1024:.2f} MB)")
        print(f"Total size: {total_size:,} bytes ({total_size/1024/1024:.2f} MB)")
        print()
        
        if dry_run:
            print("DRY RUN - No files will be uploaded")
            for name in build_files.keys():
                versioned_name = self.get_versioned_filename(name, version)
                print(f"Would upload: s3://{self.bucket_name}/{self.get_s3_folder_path(version)}/{versioned_name}")
            print(f"Would update: s3://{self.bucket_name}/fw/latest.json -> {version}")
            return True

        upload_success = True
        for name, local_path in build_files.items():
            if not self.upload_versioned_file(local_path, version, name):
                upload_success = False

        if not upload_success:
            print("ERROR: Some files failed to upload")
            return False

        # Update fw/latest.json pointer
        try:
            self.s3_client.put_object(
                Bucket=self.bucket_name,
                Key="fw/latest.json",
                Body=json.dumps({"version": version}),
                ContentType="application/json",
                ServerSideEncryption="AES256",
            )
            print(f"Updated fw/latest.json -> {version}")
        except ClientError as e:
            print(f"ERROR updating fw/latest.json: {e}")
            return False

        print(f"\n✅ Successfully uploaded firmware version {version} to S3!")
        print(f"S3 URLs:")
        for name in build_files.keys():
            versioned_name = self.get_versioned_filename(name, version)
            print(f"  - s3://{self.bucket_name}/{self.get_s3_folder_path(version)}/{versioned_name}")
        print(f"  - s3://{self.bucket_name}/fw/latest.json")

        return True


def main():
    parser = argparse.ArgumentParser(
        description="Upload EnergyMe-Home firmware to AWS S3",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 utils/s3_firmware_upload.py                              # Upload to dev with default settings
  python3 utils/s3_firmware_upload.py --dry-run                   # Show what would be uploaded
  python3 utils/s3_firmware_upload.py --aws-env prod --pio-env esp32s3-prod  # Upload release build to prod
  python3 utils/s3_firmware_upload.py --aws-env dev --pio-env esp32s3-prod   # Upload release build to dev for testing
  python3 utils/s3_firmware_upload.py --profile my-sso            # Use specific AWS SSO profile
  python3 utils/s3_firmware_upload.py --version 1.0.3-alpha       # Override version for testing
  python3 utils/s3_firmware_upload.py -v 1.0.3-dev --dry-run      # Test pre-release upload
        """
    )

    parser.add_argument(
        '--aws-env',
        default='dev',
        choices=['dev', 'prod'],
        help='AWS environment - selects S3 bucket (default: dev)'
    )

    parser.add_argument(
        '--pio-env', '--environment', '-e',
        default='esp32s3-dev',
        dest='pio_env',
        help='PlatformIO build environment (default: esp32s3-dev)'
    )

    parser.add_argument(
        '--profile', '-p',
        default='default',
        help='AWS SSO profile name (default: default)'
    )

    parser.add_argument(
        '--dry-run', '-n',
        action='store_true',
        help='Show what would be uploaded without actually uploading'
    )

    parser.add_argument(
        '--package-dir', '-d',
        default=None,
        help='Path to CI package folder containing firmware files (overrides default build dir)'
    )

    parser.add_argument(
        '--version', '-v',
        dest='version_override',
        default=None,
        help='Override version for S3 upload (e.g., "1.0.3-alpha"). Use for testing without modifying constants.h'
    )

    args = parser.parse_args()

    uploader = FirmwareUploader(
        aws_env=args.aws_env,
        environment=args.pio_env,
        profile_name=args.profile,
        package_dir=args.package_dir,
        version_override=args.version_override
    )

    success = uploader.upload_firmware(dry_run=args.dry_run)
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
