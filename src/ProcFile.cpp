
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "ProcFile.h"

// Construct the item: save parameters and allocate the buffer
ProcFileData::ProcFileData (const char path[], size_t size)
: m_size (size)
, m_fd (-1)
, m_length (-1)
, m_data (NULL)
{
    m_path = (path) ? (char*)path : (char*)"/proc/stat";
    m_data = new char[size + 1];
}

// Destroy the item: close the file and release the buffer
ProcFileData::~ProcFileData ()
{
    this->close ();
    delete [] m_data;
}

// Force the file closed so it must be reopened on the next access
void ProcFileData::close ()
{
    if (m_fd >= 0)
        ::close (m_fd);
    m_fd = -1;
}

// Refresh the data by reading/re-reading the file into m_data
bool ProcFileData::refresh ()
{
    // If the file isn't open, open it
    if (m_fd < 0)
    {
        m_fd = open (m_path, O_RDONLY, 0);
        if (m_fd < 0)
        {
            // Some very short-lived processes are normal to have ended
            // by the time of their processing
            OvlDebug("open(%s) failed, errno %d: %s",
                      m_path, errno, strerror (errno));
            return false;
        }
    }
    // Otherwise rewind the file to read again
    else
    {
        if (lseek (m_fd, 0L, SEEK_SET) == -1)
        {
            OvlError ("lseek(%s) failed, errno %d: %s",
                      m_path, errno, strerror (errno));
            return false;
        }
    }

    // Clear the buffer then read the file
    memset (m_data, 0, m_size);
    m_length = (int) read (m_fd, m_data, m_size);

    // If the read fails, close the file
    if (m_length == -1)
    {
        OvlError ("read(%s) failed, errno %d: %s",
                  m_path, errno, strerror (errno));
        this->close ();
        return false;
    }

    m_data[m_length+1] = 0;
    return true;
}

// Search the data for 'name' and return 0 if it's not found;
// otherwise return the string that _follows_ 'name' in the data
const char* ProcFileData::data_after (const char* name) const
{
    const char* cp = strstr (this->data (), name);
    if (cp != 0)
        cp += strlen (name);
    return cp;
}

// Fetch a numeric value following a name in the data (common case)
OVLValue ProcFileData::get_value (const char* name) const
{
    OVLValue value = 0;
    const char* cp = data_after (name);
    if (cp != 0)
    {
        char* end;
        value = strtoull (cp, &end, 10);
        if (end == cp)
            value = 0;
    }
    return value;
}
