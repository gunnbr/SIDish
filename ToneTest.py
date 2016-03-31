import struct
import math
import sys

BITRATE = 16000
TABLE_SIZE = 1024

LENGTH_SECONDS = 10
VBI_COUNT = BITRATE / 50
SONG_STEP_COUNT = 4
US_PER_TICK = 1000000.0 / BITRATE

# 16 bits
# The number of steps per tick
VOICE1_STEPS = 0
VOICE2_STEPS = 0
VOICE3_STEPS = 0

# 12 bits
VOICE1_DUTY = 0

# 8 bits
VOICE1_CONTROL = 0

# 8 BITS
VOICE1_ATTACKDECAY = 0

# 8 BITS
VOICE1_SUSTAINRELEASE = 0

songdata = []

class ToneGenerator():
    """Generates a raw audio file based on the tone parameters

    The format is BITRATE unsigned 8 bit PCM
    """

    global VBI_COUNT
    global TABLE_SIZE
    global SONG_STEP_COUNT
    voiceKeys = [0,0,0,0]
    voiceErrorSteps = [0,0,0,0]
    voiceCounts = [0,0,0,0]
    voiceSteps = [0,0,0,0]
    voiceErrorPercents = [0,0,0,0]
    voiceOn = [False, False, False, False]
    noise = 0x42

    # One of these for each channel that provides the offset in the songdata
    # to the orderlist for this channel
    orderlistOffset = []

    # The position in the orderlist for each channel
    orderlistPosition = []

    songPosition = []

    # The number of times remaining to repeat the current pattern before
    # moving to the next one
    patternRepeatCountdown = [0, 0, 0, 0]

    songStepCountdown = 1
    patternOffsets = []

    programDelay = 0
    totalProgSteps = 0
    songSteps = SONG_STEP_COUNT

    totalTicks = 0
    arpeggio = False
    vbiCount = VBI_COUNT
    FREQUENCY_TABLE = []
    ERRORPERCENT_TABLE = []
    SINE_TABLE = []

    samplesWritten = 0

    def __init__(self):
        print 'usPerTick {}'.format(US_PER_TICK)
        # Generate the frequency table for piano keys
        for x in range(87):
            frequency = math.pow(2, (x-49.0)/12.0) * 440.0
            steps = frequency / BITRATE * TABLE_SIZE
            self.FREQUENCY_TABLE.append(int(steps))
            errorPercent = int(math.floor((steps-int(round(steps, 2))) * 100))
            self.ERRORPERCENT_TABLE.append(errorPercent)
            #print 'Key {}: Frequency {}: Steps: {} Int Steps: {} Error Percent: {}'.format(x, frequency, steps, int(steps), errorPercent)

        #print '\n'

        #print('const uint16_t FREQUENCY_TABLE[] PROGMEM = {'),
        #for x in range(87):
        #    print("%d," % self.FREQUENCY_TABLE[x]),
        #print '};'

        #print('const uint8_t ERRORPERCENT_TABLE[] PROGMEM = {'),
        #for x in range(87):
        #    print("%d," % self.ERRORPERCENT_TABLE[x]),
        #print '};'

        # Generate sine wave table
        #print 'Sine Table\n----------'
        #print('const int8_t sineTable[] PROGMEM = {'),
        for x in range(0, TABLE_SIZE):
            y = int(32*math.sin(2*math.pi*x/TABLE_SIZE))
            #print("%d," % y),
            self.SINE_TABLE.append(y)
        #print '};\n'

    def ToneISR(self):
        global VBI_COUNT
        global TABLE_SIZE

        outValue = 0

        for channel in range(0,4):
            self.voiceCounts[channel] += self.voiceSteps[channel]
            self.voiceErrorPercents[channel] += self.voiceErrorSteps[channel]
            if self.voiceErrorPercents[channel] >= 100:
                self.voiceCounts[channel] += 1
                self.voiceErrorPercents[channel] -= 100
            if self.voiceCounts[channel] >= TABLE_SIZE:
                self.voiceCounts[channel] -= TABLE_SIZE
            if self.voiceOn[channel]:
                outValue += self.SINE_TABLE[self.voiceCounts[channel]]

        # Scale -128 to 128 values to 0 to 255
        #print 'outValue: {}'.format(outValue)
        outValue += 128

        if outValue < 0:
            outValue = 0
        if outValue > 255:
            outValue = 255

        byteout = struct.pack("@B", outValue)
        self.fp.write(byteout)
        self.samplesWritten += 1

        self.vbiCount -= 1
        if self.vbiCount == 0:
            self.vbiCount = VBI_COUNT
            return self.ProgramStep()

        return True

    def ProgramStep(self):
        #print "VBI"
        self.songStepCountdown -= 1
        if self.songStepCountdown > 0:
            # TODO Process effects here
            return True

        #print "Song step"
        self.songStepCountdown = SONG_STEP_COUNT

        print "0x%02x:" % self.totalProgSteps,

        for channel in range(0,3):
            needNote = True
            while needNote:
                needNote = False
                note = self.pgm_read_byte(self.songPosition[channel])
                print "   %02X" % note,
                if note >= 0x60 and note <= 0xBC:
                    note -= 0x60
                    print "(%d)" % note,
                    self.SetKey(channel, note)
                elif note == 0xBE:
                    self.KeyOff(channel)
                    print "(OFF)",
                elif note == 0xFF:
                    print "(END)",
                    needNote = True
                    if self.patternRepeatCountdown[channel] > 0:
                        self.patternRepeatCountdown[channel] -= 1
                        patternNumber = self.pgm_read_byte(self.orderlistOffset[channel] + self.orderlistPosition[channel])
                        self.songPosition[channel] = self.patternOffsets[patternNumber]
                        continue

                    self.orderlistPosition[channel] += 1

                    havePattern = False
                    while not havePattern:
                        # Get the pattern number from the current position
                        patternNumber = self.pgm_read_byte(self.orderlistOffset[channel] + self.orderlistPosition[channel])
                        if patternNumber >= 0xE0 and patternNumber <= 0xFE:
                            self.orderlistPosition[channel] += 1
                            # TODO: Handle transpose codes!
                        elif patternNumber >= 0xD0 and patternNumber <= 0xDF:
                            self.orderlistPosition[channel] += 1
                            repeatCount = patternNumber & 0x0F
                            if repeatCount == 0:
                                repeatCount = 16
                            self.patternRepeatCountdown[channel] = repeatCount
                        elif patternNumber == 0xFF:
                            print "End of orderlist for channel %d" % channel
                            return False
                        else:
                            havePattern = True

                    self.songPosition[channel] = self.patternOffsets[patternNumber]

            # TODO: Handle all the rest of the interesting parts
            self.songPosition[channel] += 4

        print

        self.totalProgSteps += 1
        if self.totalProgSteps % 10 == 0:
            print self.totalProgSteps

        #if self.totalProgSteps >= 128:
        #    return False

        return True

    def noise(self):
        bit = ((self.noise >> 0) ^ (self.noise >> 2) ^ (self.noise >> 3) ^ (self.noise >> 5)) & 1;
        self.noise = (self.noise >> 1) | (bit << 15);
        return self.noise & 0xFF

    def SetKey(self, channel, key):
        if channel >= 4:
            return

        self.voiceSteps[channel] = self.FREQUENCY_TABLE[key]
        self.voiceErrorSteps[channel] = self.ERRORPERCENT_TABLE[key]
        self.voiceCounts[channel] = 0
        self.voiceErrorPercents[channel] = 0
        self.voiceKeys[channel] = key
        self.voiceOn[channel] = True
        #print 'Voice {} key #{}: steps {}\n'.format(channel, key, self.voiceSteps[channel])

    def KeyOff(self, channel):
        self.voiceOn[channel] = False

    def OpenFile(self, filename):
        global BITRATE
        global LENGTH_SECONDS
        self.fp = open(filename, 'wb')

        # Initialize WAVE format
        self.fp.write("RIFF");

        # Total size of the rest of the file (4 unsigned bytes)
        value = 24 + LENGTH_SECONDS * BITRATE
        bytesout = struct.pack("<L", value)
        self.fp.write(bytesout)

        self.fp.write("WAVE")

        self.fp.write("fmt ")
        # Chunk size (2 bytes)
        value = 16
        bytesout = struct.pack("<L", value)
        self.fp.write(bytesout)

        # Format code (4 bytes)
        value = 1   # WAVE_FORMAT_PCM
        bytesout = struct.pack("<h", value)
        self.fp.write(bytesout)

        # Number of interleaved channels (2 bytes)
        value = 1 # WAVE_FORMAT_PCM
        bytesout = struct.pack("<h", value)
        self.fp.write(bytesout)

        # Sample rate (4 bytes)
        value = BITRATE
        bytesout = struct.pack("<L", value)
        self.fp.write(bytesout)

        # Data rate (average bytes per second) (4 bytes)
        # Same as bitrate since we have 1 byte per sample
        value = BITRATE
        bytesout = struct.pack("<L", value)
        self.fp.write(bytesout)

        # Data block size (2 bytes)
        value = 1
        bytesout = struct.pack("<h", value)
        self.fp.write(bytesout)

        # Bits per sample (2 bytes)
        value = 8
        bytesout = struct.pack("<h", value)
        self.fp.write(bytesout)

        self.fp.write("data")
        # Now all the rest of the data is written in the "ISR"

    def CloseFile(self):
        self.fp.seek(4)
        value = 24 + self.samplesWritten
        bytesout = struct.pack("<L", value)
        self.fp.write(bytesout)
        self.fp.close()

    def Run(self):
        print "Running the song"
        while self.ToneISR():
            pass
        print "All done!"

    def OpenSong(self, filename):
        with open(filename, 'rb') as f:
            self.songdata = f.read()
            self.offset = 0

        header = self.ReadLong()
        if header != 0x47545335:
            print "Header is 0x%08X (NOT from GoatTracker)" % header
            sys.exit()

        print "Found GoatTracker header"

        name = ""
        for i in range(0, 32):
            character = self.ReadChar()
            if character != '\0':
                name += character
        print "Song Name: %s" % name

        name = ""
        for i in range(0, 32):
            character = self.ReadChar()
            if character != '\0':
                name += character
        print "   Author: %s" % name

        name = ""
        for i in range(0, 32):
            character = self.ReadChar()
            if character != '\0':
                name += character
        print "Copyright: %s" % name

        subtunes = self.ReadByte()
        print "# subtunes: %d" % subtunes

        for i in range(0, subtunes):
            size = self.ReadByte()
            print "Subtune %d Orderlist 1 Size %d" % (i, size)
            self.orderlistOffset.append(self.offset)
            for x in range(0, size + 1):
                data = self.ReadByte()
                print "  %d: 0x%02X" % (x, data)
            # generator.offset += size
            size = self.ReadByte()
            self.orderlistOffset.append(self.offset)
            print "Subtune %d Orderlist 2 Size %d" % (i, size)
            self.offset += size + 1
            size = self.ReadByte()
            self.orderlistOffset.append(self.offset)
            print "Subtune %d Orderlist 3 Size %d" % (i, size)
            self.offset += size + 1


        numInstruments = self.ReadByte()
        print "Number of instruments: %d" % numInstruments

        for i in range(0, numInstruments):
            adsr = self.ReadShort()
            self.offset += 7
            name = ""
            for x in range(0, 16):
                data = self.ReadChar()
                if data != '\0':
                    name += data
            print "Instrument %d (%s): ADSR 0x%04X" % (i, name, adsr)

        size = self.ReadByte()
        print "Wavetable Size: %d" % size
        self.offset += size * 2

        size = self.ReadByte()
        print "Pulsetable Size: %d" % size
        self.offset += size * 2

        size = self.ReadByte()
        print "Filtertable Size: %d" % size
        self.offset += size * 2

        size = self.ReadByte()
        print "Speedtable Size: %d" % size
        self.offset += size * 2

        numPatterns = self.ReadByte()
        print "Number Patterns: %d" % numPatterns

        for i in range (0, numPatterns):
            data = self.ReadByte()
            print "Pattern %d: Rows %d: Offset: %d" % (i, data, self.offset)
            self.patternOffsets.append(self.offset)
            self.offset += 4*data

        # Initializa the song to get ready to play
        for channel in range(0, 3):
            # Start each channel at the first pattern in the order list
            self.orderlistPosition.append(0)
            havePattern = False
            while not havePattern:
                # Get the pattern number from the current position
                patternNumber = self.pgm_read_byte(self.orderlistOffset[channel] + self.orderlistPosition[channel])
                if patternNumber >= 0xE0 and patternNumber <= 0xFE:
                    self.orderlistPosition[channel] += 1
                    # TODO: Handle transpose codes!
                elif patternNumber >= 0xD0 and patternNumber <= 0xDF:
                    self.orderlistPosition[channel] += 1
                    repeatCount = patternNumber & 0x0F
                    if repeatCount == 0:
                        repeatCount = 16
                    self.patternRepeatCountdown[channel] = repeatCount
                else:
                    havePattern = True;

            # Start each channel at the first song position for each pattern
            self.songPosition.append(self.patternOffsets[patternNumber])

    def ReadLong(self):
        data = struct.unpack(">I", self.songdata[self.offset:self.offset + 4])
        self.offset += 4
        return data[0]

    def ReadShort(self):
        data = struct.unpack(">H", self.songdata[self.offset:self.offset + 2])
        self.offset += 2
        return data[0]

    def ReadChar(self):
        data = struct.unpack("c", self.songdata[self.offset:self.offset + 1])
        self.offset += 1
        return data[0]

    def ReadByte(self):
        data = struct.unpack("B", self.songdata[self.offset:self.offset + 1])
        self.offset += 1
        return data[0]

    def pgm_read_dword(self, at):
        data = struct.unpack(">I", self.songdata[at:at + 4])
        return data[0]

    def pgm_read_word(self, at):
        data = struct.unpack(">H", self.songdata[at:at + 2])
        return data[0]

    def pgm_read_byte(self, at):
        data = struct.unpack("B", self.songdata[at:at + 1])
        return data[0]

if __name__ == '__main__':
    generator = ToneGenerator()

    generator.OpenSong("Comic_Bakery.sng")

    # Open file
    generator.OpenFile('tonetest.wav')

    # Run the generator
    generator.Run()

    # Close file
    generator.CloseFile()

