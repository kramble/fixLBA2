// fixLBA2.cpp - patch LBA2.EXE to fix graphics (display pitch incorrect on Vista)

// This is an alternative to FunnyFrog's patch which has unwanted side effects (sound volume,
// broken car steering except backwards, mob paths stuck). Instead of forcing the display
// pitch (720 versus 640 width) via an LBA2 global, we fix the pitch issue by copying display
// data on the fly, using a temporary buffer, so LBA retains it's original 640 pitch/width.

#include "stdio.h"
#include "string.h"

FILE *ifile, *ofile;

// Two new sections .bss2 and .text2
// Need 4 dwords for global variables plus a buffer of 640 * 480 bytes = 16 + 307200 = 307216 (0x4B010)
// Need to round to 0x1000, so reserve 0x4C000 bytes of uninitialized storage in .bss2
// The .text2 is simpler, we use 0x200 bytes of file data (minimum raw size) which rounds up to 0x1000
const unsigned char sects[] = {
'.', 'b', 's', 's', '2', 0, 0, 0,	// Name
 0, 0, 0, 0,						// VirtSize
 0x00, 0x30, 0x0e, 0x00,			// VirtAddr (follows on from final section) and loads to 004e3000
 0x00, 0xC0, 0x04, 0x00,			// RawDataSize 0x4C000 bytes
 0, 0, 0, 0,						// RawDataOffset
 0, 0, 0, 0,						//
 0, 0, 0, 0,						//
 0, 0, 0, 0,						//
 0x80, 0x00, 0x00, 0xc0,			// Characteristics
'.', 't', 'e', 'x', 't', '2', 0, 0,	// Name
 0, 0, 0, 0,						// VirtSize
 0x00, 0xF0, 0x12, 0x00,			// VirtAddr (add the 0x4C000 bytes RawDataSize above to VirtAddr above) loads to 0052f000
 0x00, 0x02, 0x00, 0x00,			// RawDataSize
 0x00, 0x68, 0x09, 0x00,			// RawDataOffset
 0, 0, 0, 0,						//
 0, 0, 0, 0,						//
 0, 0, 0, 0,						//
 0x20, 0x00, 0x00, 0x60				// Characteristics

 };

void usage()
{
	fprintf(stderr,"usage: fixLBA2 InputFile OutputFile\n");
	fprintf(stderr,"eg:    fixLBA2 LBA2.EXE  LBA2NEW.EXE\n");
}

int main(int argc, char **argv)
{
	int verbose = 0;		// Verbosity of reporting
	int arg = 1;
	char drive = '\0';
	
	// Somewhat klunky argument processing (don't want to use getopt for portability)
	// Only works for separate switches eg -v -dE and not -vdE, switches must preceed filenames

	while (argc-arg > 0 && argv[arg][0] == '-')
	{
		if (argv[arg][1] == 'v')
		{
			verbose++;
			arg++;
			continue;
		}
		
		if (argv[arg][1] != 'd')
		{
			fprintf(stderr,"error: only switch -dX is accepted\n");
			usage();
			return 1;
		}
		drive = argv[arg][2];			// It could also be NULL, which is handled OK
		if (drive >= 'a' && drive <= 'z')
			drive += 'A' - 'a';		// Convert to uppercase
		if (drive < 'A' || drive > 'Z')
		{
			fprintf(stderr,"error: switch -dX has invalid (or missing) drive letter\n");
			usage();
			return 1;
		}
		arg++;
	}
	
	if (argc-arg != 2)
	{
		if (argc-arg < 2)
			fprintf(stderr,"error: must supply both input and output files\n");
		else if (argc-arg > 2)
			fprintf(stderr,"error: must supply no more than two files (input and output)\n");
		usage();
		return 1;
	}
	
	// Bad things happen if the same file is opened for both read and write (like all zeros output) so try
	// to prevent this. It won't catch all eventualities (eg different paths to the same file). I suppose
	// I could open the input file in exclusive mode to catch this, but that means using OS file I/O.

	if (!strcmp(argv[argc-2],argv[argc-1]))
	{
		fprintf(stderr,"error: input and output file cannot be the same\n");
		usage();
		return 1;
	}
		
	if (!(ifile = fopen(argv[argc-2],"rb")))
	{
		fprintf(stderr,"error: unable to open input file\n");
		usage();
		return 1;
	}

	if (!(ofile = fopen(argv[argc-1],"wb")))
	{
		fprintf(stderr,"error: unable to open output file\n");
		fprintf(stderr,"check you have write permissions on the folder (or run it somewhere you do)\n");
		usage();
		fclose (ifile);
		return 1;
	}
	
	// Read the input file, copying to output file as we go
	
	int a = 0;					// File offset (address)
	int checksum = 0;			// Used for verification (simple 32 bit sum)
	int checkPE = 0;			// Used for verification
	unsigned int e_lfanew = 0;	// Used for verification (offset to PE header)
	int ch;
	
	while (EOF != (ch=getc(ifile)))
	{
		// This is ugly, ugly stuff, sorry! It just happened this way and I'm too lazy to prettify it
		
		// Convert file offset to actual code address (simplifies patching the code section)
		int b = a + 0x00404000 - 0x00003C00;	// Adjusts for load address and file offset to code

		// Do some checks on the input file (we just report on it, the patch is done regardless)
		if (a == 0 && ch == 'M')		checkPE++;
		if (a == 1 && ch == 'Z')		checkPE++;
		
		// Locate the PE header (we only use this for input validation)
		if (a >= 0x3C && a <= 0x3F)		e_lfanew = (e_lfanew >> 8) | (ch << 24);
		
		// Ideally we'd use e_lfanew (see above) to locate this, but a fixed location will suffice here
		if (a == 0x3908 && ch == 'P')	checkPE++;
		if (a == 0x3909 && ch == 'E')	checkPE++;
		
		// Patch the file
		
		// First update the PE headers
		
		if (a == 0x390E)	ch = 9;				// Update section count from 7 to 9

		// Update size of code, originally 6F600 adding 0x200 for .text2, now 6F800
		if (a == 0x3924)	ch = 0;
		if (a == 0x3925)	ch = 0xF8;
		if (a == 0x3926)	ch = 0x06;

		// Update size of uninitialized data, originally 48C00 adding 4C000 for .bss2, now 94C00
		if (a == 0x392C)	ch = 0;
		if (a == 0x392D)	ch = 0x4C;
		if (a == 0x392E)	ch = 0x09;
		
		// Update size of image, originally E3000, adding 4C000 (.bss2) + 1000 (.text2 rounded up), now 130000
		if (a == 0x3958)	ch = 0;
		if (a == 0x3959)	ch = 0x00;
		if (a == 0x395A)	ch = 0x13;
		
		// Append two section table entries (0x28 bytes each), see global sects at top of file
		// Luckily there is plenty of unused space at the end of the table, so just fill in the values
		if (a >= 0x3B18 && a < 0x3B68)
		{
			int i = a - 0x3B18;
			if (i < sizeof(sects))	// sanity check
				ch = sects[i];
		}
	
		// OPTIONALLY specify CDROM drive (overrides automatic detection which does not always work)
		// NB this patch is a bit klunky as LBA2 will silently exit if the drive is not mounted

		if (drive)
		{
			// Apply patch
			if (a == 0x578F1)				ch = 0xB0;
			if (a == 0x578F2)				ch = drive;		// Drive letter
		}
		else
		{
			// Restore original code (so we can re-run the patch to reinstate it)
			if (a == 0x578F1)				ch = 0x30;
			if (a == 0x578F2)				ch = 0xC0;
		}

		// Patch the function that calls DirectDrawSurface.Lock
		// This is done similarly to FunnyFrog's patch via a minor refactoring of the code and a call to a patch area.
		// In this case the patch is in the new .text2 section. Note that the call does NOT return to the next
		// instruction at 45bb2b, but skips forwards to the instruction at 45bb35 (yeah FunnyFrog did that for a good
		// reason, obtaining a relocatable data offset via the return address, and I'm not bothering to change it)
		
		// Unlike FunnyFrog's, this patch does NOT save the lPitch value. Instead it passes an intermediate buffer (located
		// in .bss2) back to the calling function in place of the lpSurface buffer returned by Lock. Then in the second part
		// of the patch (Unlock), video data is copied into lpSurface using the lPitch correction.
		
		// The call instruction is at 45bb26 and destination is 52f000 so offset is 0052f000 - 0045bb26 - 5 = 000D34D5

		const unsigned char patch1[] = {
			0xFF, 0x05, 0xC8, 0x11, 0x4D, 0x00,	// incl 0x4D11C8 (retains same data offset as original code, so reloc is fine)
			0xE8, 0xD5, 0x34, 0x0D, 0x00		// call 0x52F000
			// NB patch makes use of (relocatable) data offset at 0x45BB2B, so don't change it!
		};
		
		if (b >= 0x45BB20 && b < 0x45BB2B)		// code offset rather than file offset (so directly comparable with disassembly)
		{
			int i = b - 0x45BB20;
			if (i < sizeof(patch1))				// sanity check
				ch = patch1[i];
		}
		
		// Patch the function that calls DirectDrawSurface.Unlock
		// This is done via a jump instruction to the patch area (it jumps back on completion)
		// The jump instruction is at 45bb63 and destination is 52f06d so offset is 0x0052f06d - 0x0045bb63 - 5 = 0x000d3505
		// The jump replaces the original code FF 92 80 00 00 00 call dword ptr [edx+00000080h]
		// This is safe from relocation (lucky), but one byte longer so add a nop to keep any disassembly in sync

		const unsigned char patch2[] = {
			0xE9, 0x05, 0x35, 0x0D, 0x00,		// jmp 0x52F06D
			0x90								// nop (to keep disassembly in sync)
		};
		
		if (b >= 0x45BB63 && b < 0x45BB69)		// code offset rather than file offset (so directly comparable with disassembly)
		{
			int i = b - 0x45BB63;
			if (i < sizeof(patch2))				// sanity check
				ch = patch2[i];
		}
		
		// Calculate a simple checksum of output (ideally we'd use a digest)
		// Accounting for drive would complicate verifying the result, so just fudge it
		if (a == 0x578F1)
			checksum += 0x30;
		else if (a == 0x578F2)
			checksum += 0xC0;
		else
			checksum += ch;
		
		// Write byte
		putc(ch, ofile);
		a++;
		
		// The original LBA2.EXE ends at offset 0x967FF, but allow that the input file may have already had this
		// patch applied and is 0x200 bytes longer, so stop copying at this point so that .text2 is properly appended
		// (This does have a practical use, to change the -d switch setting)
		if (a == 0x96800)
			break;
	}

	// Report whether the input file looks OK (a very basic check)

	verbose && fprintf(stderr,"info: checkPE = %d e_lfanew = 0x%08X\n", checkPE, e_lfanew);
	
	if (checkPE < 2)
		verbose && fprintf(stderr,"warning: input file does not look like a valid Windows Executable\n");
	else if (e_lfanew != 0x3908 || checkPE < 4)
		verbose && fprintf(stderr,"warning: input file does not look like LBA2\n");
	
	if (a != 0x96800)
	{
		verbose && fprintf(stderr,"warning: input file was unexpectedly short\n");
		// But we'll pad it out anyway so the new text section is in the right place
		while (a++ < 0x96800)
		{
			putc(0, ofile);
			// checksum += ch;		// Is unneccessary here as ch==0
		}
	}
	
	// Append 0x200 bytes of data for new text section (size to match section header above)
	// Code starts at file offset 00096800 which loads to 00400000 + 0012f000 (see sects) = 0052f000
	// NB the code array is actually less than 0x200 bytes, but is padded out below
	// I'm not going to bother to include any annotations (the source file is way too messy to copy directly),
	// but it's all fairly straightforward (with a few tricks to hopefully ensure it survives any relocation)
	const unsigned char text2code[] = {
0x8B, 0x14, 0x24, 0x8B, 0x0A, 0x81, 0xC1, 0x00, 0xC3, 0x06, 0x00, 0x8B, 0x5D, 0x88, 0x81, 0xE3,
0x0E, 0x00, 0x00, 0x00, 0x81, 0xFB, 0x0E, 0x00, 0x00, 0x00, 0x75, 0x35, 0x8B, 0x5D, 0x8C, 0x81,
0xFB, 0xE0, 0x01, 0x00, 0x00, 0x75, 0x2A, 0x89, 0x19, 0x8B, 0x5D, 0x90, 0x81, 0xFB, 0x80, 0x02,
0x00, 0x00, 0x75, 0x1D, 0x89, 0x59, 0x04, 0x8B, 0x5D, 0x94, 0x89, 0x59, 0x08, 0x8B, 0x5D, 0xA8,
0x89, 0x59, 0x0C, 0x81, 0xC1, 0x10, 0x00, 0x00, 0x00, 0x89, 0x4D, 0xA8, 0xE9, 0x10, 0x00, 0x00,
0x00, 0xBB, 0x00, 0x00, 0x00, 0x00, 0x89, 0x19, 0x89, 0x59, 0x04, 0x89, 0x59, 0x08, 0x89, 0x59,
0x0C, 0x8B, 0x0A, 0x8B, 0x45, 0xA8, 0x89, 0x01, 0x83, 0x04, 0x24, 0x0A, 0xC3, 0x9C, 0x53, 0x51,
0x52, 0x56, 0x57, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x5A, 0x81, 0xC2, 0x88, 0x3F, 0xFB, 0xFF, 0x81,
0x3A, 0xE0, 0x01, 0x00, 0x00, 0x75, 0x30, 0x81, 0x7A, 0x04, 0x80, 0x02, 0x00, 0x00, 0x75, 0x27,
0xFC, 0x89, 0xD6, 0x81, 0xC6, 0x10, 0x00, 0x00, 0x00, 0x8B, 0x7A, 0x0C, 0x81, 0xFF, 0x00, 0x00,
0x00, 0x00, 0x74, 0x13, 0x8B, 0x1A, 0x8B, 0x4A, 0x04, 0xC1, 0xE9, 0x02, 0xF3, 0xA5, 0x03, 0x7A,
0x08, 0x2B, 0x7A, 0x04, 0x4B, 0x75, 0xEF, 0x5F, 0x5E, 0x5A, 0x59, 0x5B, 0x9D, 0xFF, 0x92, 0x80,
0x00, 0x00, 0x00, 0xE9, 0xA1, 0xCA, 0xF2, 0xFF
	};
	
	for (int i=0; i<0x200; i++)
	{
		ch = 0;							// default to zero for padding
		if (i < sizeof(text2code))		// sanity check
				ch = text2code[i];
		putc(ch, ofile);
		checksum += ch;
	}

	// Report on success based on checksum (not very good, should really use a digest)
	
	// NB the checksum does not include the effect of -d (drive), so is not the full file checksum in that case
	verbose && fprintf(stderr,"info: checksum = %d (0x%08X)\n", checksum, checksum);

	if ((checkPE == 4) && ((checksum == 0x0366FC25) || (checksum == 0x03670396)))
		fprintf(stderr,"info: patch applied successfully%s\n",
		verbose ? (checksum == 0x0366FC25) ? " to original LBA2.EXE" : " to FunnyFrog's patch of LBA2.EXE" : "");
	else
		fprintf(stderr,"warning: unexpected input file content, patch is unlikely to work\n");
	
	fclose (ifile);
	fclose (ofile);
	return 0;
}