#!/usr/bin/env python3
"""
S3 Firmware Upload Script for EnergyMe-Home

This script:
1. Parses the firmware version from constants.h
2. Uploads firmware files (bin, elf, partitions) to AWS S3
3. Generates an OTA update JSON document

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

import os
import sys
import re
import json
import argparse
from pathlib import Path
import boto3
from botocore.exceptions import ClientError
from tempfile import NamedTemporaryFile

class FirmwareUploader:
    def __init__(self, bucket_name="energyme-home-firmware-updates", environment="esp32s3-dev", profile_name="default"):
        self.bucket_name = bucket_name
        self.environment = environment
        self.profile_name = profile_name
        self.project_root = Path(__file__).parent.parent
        self.build_dir = self.project_root / ".pio" / "build" / environment
        self.constants_file = self.project_root / "include" / "constants.h"
        
        # Initialize S3 client with AWS SSO profile
        try:
            print(f"Using AWS SSO profile: {self.profile_name}")
            
            # Create a session with the specified profile
            session = boto3.Session(profile_name=self.profile_name)
            
            # Get region from session or use default
            aws_region = session.region_name or 'us-east-1'
            
            # Create S3 client using the session
            self.s3_client = session.client('s3')
            print(f"✅ AWS S3 client initialized for region: {aws_region}")
            
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
        """Check if all required build files exist"""
        required_files = {
            'firmware.bin': self.build_dir / "firmware.bin",
            'firmware.elf': self.build_dir / "firmware.elf", 
            'partitions.bin': self.build_dir / "partitions.bin"
        }
        
        missing_files = []
        for name, path in required_files.items():
            if not path.exists():
                missing_files.append(str(path))
        
        if missing_files:
            print("ERROR: Missing build files:")
            for file in missing_files:
                print(f"  - {file}")
            print(f"\nPlease run 'platformio run --environment {self.environment}' first")
            return False, {}
        
        return True, required_files
    
    def get_versioned_filename(self, base_filename, version):
        """Generate version-specific filename (e.g., energyme_home_0.12.40.bin)"""
        name, ext = base_filename.rsplit('.', 1)
        # Convert firmware.bin -> energyme_home_VERSION.bin
        if name == 'firmware':
            return f"energyme_home_{version}.{ext}"
        # Convert partitions.bin -> energyme_home_partitions_VERSION.bin
        else:
            return f"energyme_home_{name}_{version}.{ext}"
    
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

    def upload_file_to_both_locations(self, local_path, version, base_filename):
        """Upload a file to both version-specific and latest folders with versioned filenames"""
        versioned_filename = self.get_versioned_filename(base_filename, version)
        
        version_key = f"{version}/{versioned_filename}"
        latest_key = f"latest/{versioned_filename}"
        
        # Upload to version-specific folder
        if not self.upload_file_to_s3(local_path, version_key):
            return False
        
        # Upload to latest folder
        if not self.upload_file_to_s3(local_path, latest_key):
            return False
        
        return True
    
    def create_ota_json(self, version):
        """Create OTA update JSON document (version-specific)"""
        versioned_firmware = self.get_versioned_filename('firmware.bin', version)
        firmware_url = f"${{aws:iot:s3-presigned-url:https://{self.bucket_name}.s3.amazonaws.com/{version}/{versioned_firmware}}}"
        
        ota_json = {
            "operation": "ota_update",
            "firmware": {
                "version": version,
                "url": firmware_url
            }
        }
        
        return ota_json

    def create_latest_ota_json(self, version):
        """Create OTA update JSON document for latest folder (CONSTANT FILENAME)"""
        versioned_firmware = self.get_versioned_filename('firmware.bin', version)
        firmware_url = f"${{aws:iot:s3-presigned-url:https://{self.bucket_name}.s3.amazonaws.com/latest/{versioned_firmware}}}"
        
        ota_json = {
            "operation": "ota_update",
            "firmware": {
                "version": version,
                "url": firmware_url
            }
        }
        
        return ota_json
    
    def upload_firmware(self, dry_run=False):
        """Main upload function"""
        print("=== EnergyMe-Home Firmware S3 Upload ===")
        print(f"Environment: {self.environment}")
        print(f"S3 Bucket: {self.bucket_name}")
        print(f"Build directory: {self.build_dir}")
        print()
        
        # Verify S3 access
        if not dry_run and not self.verify_s3_access():
            return False
        
        # Parse version
        try:
            version = self.parse_version_from_constants()
        except Exception as e:
            print(f"ERROR parsing version: {e}")
            return False
        
        # Check build files
        files_exist, build_files = self.check_build_files_exist()
        if not files_exist:
            return False
        
        # Display file sizes
        print("Build files to upload:")
        total_size = 0
        for name, path in build_files.items():
            size = path.stat().st_size
            total_size += size
            print(f"  - {name}: {size:,} bytes ({size/1024/1024:.2f} MB)")
        print(f"Total size: {total_size:,} bytes ({total_size/1024/1024:.2f} MB)")
        print()
        
        if dry_run:
            print("DRY RUN - No files will be uploaded")
            # Show version-specific uploads
            for name in build_files.keys():
                versioned_name = self.get_versioned_filename(name, version)
                print(f"Would upload: s3://{self.bucket_name}/{version}/{versioned_name}")
            
            # Show latest folder uploads
            for name in build_files.keys():
                versioned_name = self.get_versioned_filename(name, version)
                print(f"Would upload: s3://{self.bucket_name}/latest/{versioned_name}")
            
            ota_json = self.create_ota_json(version)
            latest_ota_json = self.create_latest_ota_json(version)
            print(f"\nWould create version-specific OTA JSON:")
            print(json.dumps(ota_json, indent=2))
            print(f"Would upload: s3://{self.bucket_name}/{version}/ota-job-document.json")
            print(f"\nWould create latest OTA JSON (CONSTANT FILENAME):")
            print(json.dumps(latest_ota_json, indent=2))
            print(f"Would upload: s3://{self.bucket_name}/latest/ota-job-document.json")
            return True
        
        # Upload files to S3 (both version and latest folders)
        upload_success = True
        for name, local_path in build_files.items():
            if not self.upload_file_to_both_locations(local_path, version, name):
                upload_success = False
        
        if not upload_success:
            print("ERROR: Some files failed to upload")
            return False
        
        # Create and display OTA JSON documents
        ota_json = self.create_ota_json(version)
        latest_ota_json = self.create_latest_ota_json(version)
        print(f"\nVersion-specific OTA Update JSON Document:")
        print(json.dumps(ota_json, indent=2))
        print(f"\nLatest OTA Update JSON Document:")
        print(json.dumps(latest_ota_json, indent=2))
        
        # Upload version-specific OTA JSON to S3
        ota_json_key = f"{version}/ota-job-document.json"
        try:
            # Save JSON to a temporary file for upload
            with NamedTemporaryFile("w", delete=False) as tmp_json:
                json.dump(ota_json, tmp_json, indent=2)
                tmp_json_path = tmp_json.name
            if self.upload_file_to_s3(Path(tmp_json_path), ota_json_key):
                print(f"\nVersion-specific OTA JSON uploaded to: s3://{self.bucket_name}/{ota_json_key}")
            else:
                print(f"ERROR: Failed to upload version-specific OTA JSON to S3")
                upload_success = False
            os.remove(tmp_json_path)
        except Exception as e:
            print(f"ERROR uploading version-specific OTA JSON: {e}")
            upload_success = False
        
        # Upload latest OTA JSON to S3
        latest_ota_json_key = "latest/ota-job-document.json"
        try:
            # Save JSON to a temporary file for upload
            with NamedTemporaryFile("w", delete=False) as tmp_json:
                json.dump(latest_ota_json, tmp_json, indent=2)
                tmp_json_path = tmp_json.name
            if self.upload_file_to_s3(Path(tmp_json_path), latest_ota_json_key):
                print(f"Latest OTA JSON uploaded to: s3://{self.bucket_name}/{latest_ota_json_key}")
            else:
                print(f"ERROR: Failed to upload latest OTA JSON to S3")
                upload_success = False
            os.remove(tmp_json_path)
        except Exception as e:
            print(f"ERROR uploading latest OTA JSON: {e}")
            upload_success = False
        
        if not upload_success:
            return False
        
        print(f"\n✅ Successfully uploaded firmware version {version} to S3!")
        print(f"Version-specific S3 URLs:")
        for name in build_files.keys():
            versioned_name = self.get_versioned_filename(name, version)
            print(f"  - https://{self.bucket_name}.s3.amazonaws.com/{version}/{versioned_name}")
        print(f"  - https://{self.bucket_name}.s3.amazonaws.com/{ota_json_key}")
        
        print(f"\nLatest S3 URLs:")
        for name in build_files.keys():
            versioned_name = self.get_versioned_filename(name, version)
            print(f"  - https://{self.bucket_name}.s3.amazonaws.com/latest/{versioned_name}")
        print(f"  - https://{self.bucket_name}.s3.amazonaws.com/{latest_ota_json_key}")
        
        return True


def main():
    parser = argparse.ArgumentParser(
        description="Upload EnergyMe-Home firmware to AWS S3",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 utils/s3_firmware_upload.py                    # Upload with default settings
  python3 utils/s3_firmware_upload.py --dry-run          # Show what would be uploaded
  python3 utils/s3_firmware_upload.py --bucket my-bucket # Use custom bucket
  python3 utils/s3_firmware_upload.py --env esp32s3-dev  # Use specific environment
  python3 utils/s3_firmware_upload.py --profile my-sso   # Use specific AWS SSO profile
        """
    )
    
    parser.add_argument(
        '--bucket', '-b',
        default='energyme-home-firmware-updates',
        help='S3 bucket name (default: energyme-home-firmware-updates)'
    )
    
    parser.add_argument(
        '--environment', '--env', '-e',
        default='esp32s3-dev',
        help='PlatformIO environment (default: esp32s3-dev)'
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
    
    args = parser.parse_args()
    
    # Create uploader and run
    uploader = FirmwareUploader(
        bucket_name=args.bucket,
        environment=args.environment,
        profile_name=args.profile
    )
    
    success = uploader.upload_firmware(dry_run=args.dry_run)
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
