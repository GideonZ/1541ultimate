*** REL files and their expected behavior ***

This description uses the following pseudo functions to access the drive:
* void open_file(int channel, const char *filename)          // Uses the file open token under ATN, then sends the filename; last byte with EOI
* void close_file(int channel)                               // Uses the file close token under ATN
* void send_data(int channel, const uint8_t *data, int len)  // Sends data to any channel, last byte will be sent with EOI: sends the 
* int  receive_data(int channel, uint8_t *data, int max_len) // Receives data from the channel and stores it in *data; at most max_len bytes, but shorter when EOI is encountered; returns number of bytes read.

-> Sending a command is done using "send_data" with channel = 15.
-> Reading the status channel is done using "receive_data" with channel = 15.

## File Open
To create or open a relative file, send the file configuration string over the chosen data channel (channels 2–14). The drive will automatically allocate internal buffers and administrative side-sectors based on the length parameter.

* open_file(2, "MYDATA,L,\x28) // Open file on channel 2, relative ,L and the length of each record is encoded in the byte after the second comma; \x28 is thus a record length of 40 bytes in this example.
* Behavior: 
- - If the record length is not given, the relative file should exist. If it exists, the size of each record is determined from the existing file. If it does not exist, a "62,FILE NOT FOUND" error is generated.
- - If the record length is given and the file does not exist, a new file is created with a size of 0 records. The record length is stored as meta data of the file.
- - If the record length is given and the file exists, and the record length does not match, the file is opened and a "50,RECORD NOT PRESENT" error is generated.
- - If the record length is given and the file exists, and the record length does match, the file is opened without modification. The pointer is set to the first record.

## File Close
To free the drive channels, buffers, and properly flush any unwritten data from the internal DOS track caches, close both the data channel and the command channel.

* close_file(2) // (Closes data channel 2)
* Behavior: The drive flushes its active buffer to disk. If new records were added but not yet written to physical sectors, the side-sectors are updated and finalized.

## Pointer Modification (Positioning)
To select a specific record and byte offset for subsequent read or write actions, transmit a 5-byte binary packet over the command channel.

* send_data(15, "P" + uint8_t(file channel) + uint8_t(rec_low) + uint8_t(rec_high) + uint8_t(offset), 5)
* Parameters: file_channel is the channel where the relative file is opened. rec_low and rec_high form the 16-bit record number (1–65535). offset (1–254) is the byte position within the record.

## Reading Records
To retrieve data from a record, position the pointer first, then switch the data channel to a talking state.

* send_data(15, "P" + uint8_t(2) + uint8_t(rec_low) + uint8_t(rec_high) + uint8_t(1), 5)
* len = receive_data(2, out_data, max_len)
* Behavior: The drive transmits the record data sequentially until it reaches the end of the record or encounters a terminating character, ending the transmission with an EOI signal. If the pointer is positioned beyond the current end of the file, the drive will generate a "50,RECORD NOT PRESENT" status and the file is extended. Proceding to read will then return an empty record (0xFF, followed by 0x00's). Note that these 0x00s are not transmitted over IEC when using INPUT# in basic, as 0x00 is seen as a terminating character. 

## Writing and Overwriting Records
To modify or populate a record, position the pointer first, then send the raw data bytes over the active data channel.

* send_data(15, "P" + uint8_t(2) + uint8_t(rec_low) + uint8_t(rec_high) + uint8_t(1), 5)
* send_data(2, "MY NEW PAYLOAD DATA", length)
* Behavior: Writing explicitly overwrites the existing bytes at the pointer position. If the payload is shorter than the defined record length, the remaining bytes in that record are cleared with zeros. To separate fields within a single record, you must manually inject carriage returns (0x0D) or commas into your payload.

## File Extension and Truncation Behavior
Relative files behave dynamically when growing, but require structural commands to shrink.

* Extension: If you position the pointer (P) to a record number larger than the current file length and execute a write (send_data), the drive automatically extends the file. It generates all intermediate records, filling them with empty record indicators (0xFF, followed by 0x00's), and updates the side-sectors. The drive will generate a "50,RECORD NOT PRESENT" error in this case.
* Truncation: Relative files never automatically truncate. Overwriting an existing record or writing a shorter payload does not shrink the file size on disk. To truncate a relative file, you must explicitly create a new file and manually copy the valid records over.

## Reading from the IEC channel
Relative files behave differently from linear files on the disk. Reading the data channel through IEC is dependent on the data that is stored in the records:
* When reading just one char at a time, the channel will return each byte of a record, until a zero is encountered. The drive will then automatically advance to the first byte of the next record. The reading program cannot tell that this happens; it just does.
* When using the INPUT# function, the string that is being read will terminate on a 0x00 or a CR (0x0D). Also here, when a 0x00 is encountered, the pointer is advanced to the next record, such that subsequent INPUT# commands will read data from the next record.
