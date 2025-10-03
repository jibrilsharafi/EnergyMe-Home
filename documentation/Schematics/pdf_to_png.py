"""
PDF to PNG Converter
Extracts each page of a PDF file to individual PNG images.
"""

import sys
from pathlib import Path

try:
    from pdf2image import convert_from_path
except ImportError:
    print("Error: pdf2image library not found.")
    print("Please install it using: pip install pdf2image")
    print("Note: You also need poppler installed on your system.")
    print("Windows: Download from https://github.com/oschwartz10612/poppler-windows/releases/")
    sys.exit(1)


def pdf_to_png(pdf_path, output_folder=None, dpi=300):
    """
    Convert PDF pages to PNG images.
    
    Args:
        pdf_path: Path to the PDF file
        output_folder: Folder to save PNG files (default: same as PDF)
        dpi: Resolution of output images (default: 300)
    """
    pdf_file = Path(pdf_path)
    
    if not pdf_file.exists():
        print(f"Error: PDF file not found: {pdf_path}")
        return
    
    if not pdf_file.suffix.lower() == '.pdf':
        print(f"Error: File is not a PDF: {pdf_path}")
        return
    
    # Set output folder
    if output_folder is None:
        output_folder = pdf_file.parent
    else:
        output_folder = Path(output_folder)
    
    output_folder.mkdir(parents=True, exist_ok=True)
    
    print(f"Converting {pdf_file.name} to PNG images...")
    print(f"Output folder: {output_folder}")
    print(f"Resolution: {dpi} DPI")
    
    try:
        # Convert PDF to images
        images = convert_from_path(pdf_path, dpi=dpi)
        
        # Save each page as PNG
        base_name = pdf_file.stem
        for i, image in enumerate(images, start=1):
            output_path = output_folder / f"{base_name}_page_{i}.png"
            image.save(output_path, 'PNG')
            print(f"Saved: {output_path.name}")
        
        print(f"\nSuccess! Converted {len(images)} page(s).")
        
    except Exception as e:
        print(f"Error during conversion: {e}")


if __name__ == "__main__":
    # Default to Schematics.pdf in the same directory
    script_dir = Path(__file__).parent
    default_pdf = script_dir / "Schematics.pdf"
    
    if len(sys.argv) > 1:
        pdf_path = sys.argv[1]
    elif default_pdf.exists():
        pdf_path = default_pdf
        print(f"Using default file: {default_pdf.name}")
    else:
        print("Usage: python pdf_to_png.py <pdf_file> [output_folder] [dpi]")
        print("Example: python pdf_to_png.py Schematics.pdf output 300")
        sys.exit(1)
    
    output_folder = sys.argv[2] if len(sys.argv) > 2 else None
    dpi = int(sys.argv[3]) if len(sys.argv) > 3 else 300
    
    pdf_to_png(pdf_path, output_folder, dpi)
