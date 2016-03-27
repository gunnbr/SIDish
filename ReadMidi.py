import struct


class MidiReader():
    offset = 0
    VBI_RATE = 50
    usPerProgStep = 0
    accumulatedTicks = 0
    tempo = 0
    NOTE_TABLE = []

    def __init__(self, filename):
        self.fp = open(filename, 'rb')

        # MIDI files are generally small, so it should be okay to read it all in at once
        self.midi_data = self.fp.read()
        print "Read %d bytes of MIDI data" % len(self.midi_data)

        self.fp.close()

        self.usPerProgStep = 1000000 / self.VBI_RATE
        print "usPerProgStep: %d" % self.usPerProgStep

    def Parse(self):
        self.ReadHeader()

        self.ReadTrack()

    def ReadHeader(self):
        chunk = struct.unpack('>I', self.midi_data[self.offset:self.offset+4])[0]
        if chunk != 0x4D546864: # "MThr"
            print "Found invalid header type 0x%X" % chunk
            print type(chunk)
            raise Exception('invalid header')
        self.offset += 4

        size = struct.unpack('>I', self.midi_data[self.offset:self.offset+4])[0]
        self.offset += 4
        print "Header size %d" % size

        self.division = struct.unpack('>H', self.midi_data[self.offset+4:self.offset+6])[0]
        print "Division %d" % self.division

        self.offset += size

    def ReadTrack(self):
        chunk = struct.unpack('>I', self.midi_data[self.offset:self.offset+4])[0]
        self.offset += 4
        if chunk != 0x4d54726B: # "MTrk"
            print "Found invalid track type '%s'" % chunk
            raise Exception('invalid header')

        size = struct.unpack('>I', self.midi_data[self.offset:self.offset+4])[0]
        self.offset += 4
        print "Track size %d (0x%X)" % (size, size)

        chunkEnd = self.offset + size
        maxDelay = 0
        maxAccumulated = 0
        while self.offset < chunkEnd:
            deltaTime = self.ReadVariableLengthQuantity()
            if deltaTime == 0:
                print "Delay: %d ProgSteps: 0" % deltaTime,
                progSteps = 0
            else:
                progSteps = deltaTime * self.tempo / self.division / self.usPerProgStep
                print "Delay: %d ProgSteps: %d" % (deltaTime, progSteps),
            self.accumulatedTicks += progSteps
            if deltaTime > maxDelay:
                maxDelay = deltaTime
            if self.accumulatedTicks > maxAccumulated:
                maxAccumulated = self.accumulatedTicks
            self.ReadEvent()

        print "Max delay: %d" % maxDelay
        print "Max accumulated: %d" % maxAccumulated
        print "\n# Delay, On/Off, channel, key"
        print "NOTE_TABLE = ",
        print self.NOTE_TABLE

    def ReadEvent(self):
        event = struct.unpack('B', self.midi_data[self.offset:self.offset+1])[0]
        self.offset += 1

        if event == 0xFF:
            type = struct.unpack('B', self.midi_data[self.offset:self.offset+1])[0]
            self.offset += 1
            length = self.ReadVariableLengthQuantity()
            print " Length: %d (0x%X)" % (length, length)
            if type == 0x51:
                data = struct.unpack('BBB', self.midi_data[self.offset:self.offset+3])
                self.offset += 3
                self.tempo = data[0] << 16 | data[1] << 8 | data[2]
                print "Meta Type: Set Tempo: %d uS/quarter note" % self.tempo
                return

            print "Meta Type: 0x%X" % type
            self.offset += length
            return

        if event & 0xF0 == 0x80:
            key = struct.unpack('B', self.midi_data[self.offset:self.offset+1])[0]
            self.offset += 1
            velocity = struct.unpack('B', self.midi_data[self.offset:self.offset+1])[0]
            self.offset += 1
            print "NOTE OFF Channel %d: Key %d, Velocity %d" % (event & 0x0F, key, velocity)
            noteEntry = [ self.accumulatedTicks, 0, event & 0x0f, key]
            self.NOTE_TABLE += noteEntry
            self.accumulatedTicks = 0
            return

        if event & 0xF0 == 0x90:
            key = struct.unpack('B', self.midi_data[self.offset:self.offset+1])[0]
            self.offset += 1
            velocity = struct.unpack('B', self.midi_data[self.offset:self.offset+1])[0]
            self.offset += 1
            print "NOTE ON Channel %d: Key %d, Velocity %d" % (event & 0x0F, key, velocity)
            noteEntry = [ self.accumulatedTicks, 1, event & 0x0f, key]
            self.NOTE_TABLE += noteEntry
            self.accumulatedTicks = 0
            return

        if event & 0xF0 == 0xB0:
            controllerNum = struct.unpack('B', self.midi_data[self.offset:self.offset+1])[0]
            self.offset += 1
            value = struct.unpack('B', self.midi_data[self.offset:self.offset+1])[0]
            self.offset += 1
            print "Control Change Channel %d: %d, %d" % (event & 0x0F, controllerNum, value)
            return

        if event & 0xF0 == 0xC0:
            program = struct.unpack('B', self.midi_data[self.offset:self.offset+1])[0]
            self.offset += 1
            print "Program Change Channel %d: %d" % (event & 0x0F, program)
            return

        if event & 0xF0 == 0xE0:
            low = struct.unpack('B', self.midi_data[self.offset:self.offset+1])[0]
            self.offset += 1
            high = struct.unpack('B', self.midi_data[self.offset:self.offset+1])[0]
            self.offset += 1
            value = (low | (high << 7))
            print "Pitch Wheel Change Channel %d: 0x%X" % (event & 0x0F, value)
            return

        raise Exception("Unhandled event" % event)

    def ReadVariableLengthQuantity(self):
        value = 0
        shift = 1
        while True:
            data = struct.unpack('B', self.midi_data[self.offset:self.offset+1])[0]
            self.offset += 1
            value += (data & 127) * shift
            if (data & 128) == 0 :
                break
            shift <<= 7
            value += shift
        return value

if __name__ == '__main__':
    reader = MidiReader("./Comic_Bakery.mid")
    reader.Parse()