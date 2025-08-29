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
    pip install boto3 python-dotenv

AWS Credentials:
    Create a .env file in the project root with:
    AWS_ACCESS_KEY_ID=your_access_key
    AWS_SECRET_ACCESS_KEY=your_secret_key
    AWS_SESSION_TOKEN=your_session_token  # Optional for temporary credentials
    AWS_REGION=us-east-1  # Optional, defaults to us-east-1
"""

import os
import sys
import re
import json
import argparse
from pathlib import Path
import boto3
from botocore.exceptions import ClientError, NoCredentialsError
from dotenv import load_dotenv
from tempfile import NamedTemporaryFile

# TODO: upload both as the version name, and as LATEST

class FirmwareUploader:
    def __init__(self, bucket_name="energyme-home-firmware-updates", environment="esp32dev"):
        self.bucket_name = bucket_name
        self.environment = environment
        self.project_root = Path(__file__).parent.parent
        self.build_dir = self.project_root / ".pio" / "build" / environment
        self.constants_file = self.project_root / "include" / "constants.h"
        
        # Load environment variables from .env file
        env_file = self.project_root / ".env"
        if env_file.exists():
            load_dotenv(env_file)
            print(f"Loaded environment variables from: {env_file}")
        else:
            print(f"WARNING: .env file not found at {env_file}")
            print("Please create a .env file with your AWS credentials:")
            print("AWS_ACCESS_KEY_ID=your_access_key")
            print("AWS_SECRET_ACCESS_KEY=your_secret_key")
            print("AWS_SESSION_TOKEN=your_session_token  # Optional for temporary credentials")
            print("AWS_REGION=us-east-1  # Optional")
        
        # Initialize S3 client with credentials from environment
        try:
            # Get AWS credentials from environment variables
            aws_access_key_id = os.getenv('AWS_ACCESS_KEY_ID')
            aws_secret_access_key = os.getenv('AWS_SECRET_ACCESS_KEY')
            aws_session_token = os.getenv('AWS_SESSION_TOKEN')  # Optional for temporary credentials
            aws_region = os.getenv('AWS_REGION', 'us-east-1')  # Default to us-east-1
            
            if not aws_access_key_id or not aws_secret_access_key:
                raise NoCredentialsError()
            
            # Create S3 client with explicit credentials
            session_kwargs = {
                'aws_access_key_id': aws_access_key_id,
                'aws_secret_access_key': aws_secret_access_key,
                'region_name': aws_region
            }
            
            # Add session token if available (for temporary credentials)
            if aws_session_token:
                session_kwargs['aws_session_token'] = aws_session_token
                print("Using temporary AWS credentials (with session token)")
            else:
                print("Using permanent AWS credentials")
            
            self.s3_client = boto3.client('s3', **session_kwargs)
            print(f"AWS S3 client initialized for region: {aws_region}")
            
        except NoCredentialsError:
            print("ERROR: AWS credentials not found in .env file.")
            print("Please create a .env file in the project root with:")
            print("AWS_ACCESS_KEY_ID=your_access_key")
            print("AWS_SECRET_ACCESS_KEY=your_secret_key")
            print("AWS_SESSION_TOKEN=your_session_token  # Optional for temporary credentials")
            print("AWS_REGION=us-east-1  # Optional")
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
        
        version = f"{major_match.group(1)}.{minor_match.group(1)}.{patch_match.group(1)}"
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
    
    def create_ota_json(self, version):
        """Create OTA update JSON document"""
        firmware_url = f"${{aws:iot:s3-presigned-url:https://{self.bucket_name}.s3.amazonaws.com/{version}/firmware.bin}}"
        
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
            s3_keys = [f"{version}/{name}" for name in build_files.keys()]
            for key in s3_keys:
                print(f"Would upload: s3://{self.bucket_name}/{key}")
            
            ota_json = self.create_ota_json(version)
            print(f"\nWould create OTA JSON:")
            print(json.dumps(ota_json, indent=2))
            print(f"Would upload: s3://{self.bucket_name}/{version}/ota-job-document.json")
            return True
        
        # Upload files to S3
        upload_success = True
        for name, local_path in build_files.items():
            s3_key = f"{version}/{name}"
            if not self.upload_file_to_s3(local_path, s3_key):
                upload_success = False
        
        if not upload_success:
            print("ERROR: Some files failed to upload")
            return False
        
        # Create and display OTA JSON
        ota_json = self.create_ota_json(version)
        print(f"\nOTA Update JSON Document:")
        print(json.dumps(ota_json, indent=2))
        
        # Upload OTA JSON to S3
        ota_json_key = f"{version}/ota-job-document.json"
        try:
            # Save JSON to a temporary file for upload
            with NamedTemporaryFile("w", delete=False) as tmp_json:
                json.dump(ota_json, tmp_json, indent=2)
                tmp_json_path = tmp_json.name
            if self.upload_file_to_s3(Path(tmp_json_path), ota_json_key):
                print(f"\nOTA JSON uploaded to: s3://{self.bucket_name}/{ota_json_key}")
            else:
                print(f"ERROR: Failed to upload OTA JSON to S3")
            os.remove(tmp_json_path)
        except Exception as e:
            print(f"ERROR uploading OTA JSON: {e}")
            return False
        
        print(f"\n✅ Successfully uploaded firmware version {version} to S3!")
        print(f"S3 URLs:")
        for name in build_files.keys():
            print(f"  - https://{self.bucket_name}.s3.amazonaws.com/{version}/{name}")
        print(f"  - https://{self.bucket_name}.s3.amazonaws.com/{ota_json_key}")
        
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
  python3 utils/s3_firmware_upload.py --env esp32dev     # Use specific environment
        """
    )
    
    parser.add_argument(
        '--bucket', '-b',
        default='energyme-home-firmware-updates',
        help='S3 bucket name (default: energyme-home-firmware-updates)'
    )
    
    parser.add_argument(
        '--environment', '--env', '-e',
        default='esp32dev',
        help='PlatformIO environment (default: esp32dev)'
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
        environment=args.environment
    )
    
    success = uploader.upload_firmware(dry_run=args.dry_run)
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
