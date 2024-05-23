/*
-----------------------------------------------------------------------------
    ProcFileData
    Common handling to open/rewind and read a file

    Author: CostisC
-----------------------------------------------------------------------------
*/

#ifndef PROCFILE_H
#define PROCFILE_H

#include <stdio.h>

typedef unsigned long long OVLValue;

#define FILEBUFF    4 * 1024


#define OvlLOG(msg, LVL, ...) \
        fprintf (stderr, "%s - %s (ln. %d): " msg "\n", LVL, __FILE__, __LINE__, ##__VA_ARGS__)

#define OvlError(msg, ...) OvlLOG(msg, "ERROR", ##__VA_ARGS__)
#define OvlWarn(msg, ...) OvlLOG(msg, "WARN", ##__VA_ARGS__)
#define OvlInfo(msg, ...) \
        fprintf (stderr,  "INFO: " msg "\n", ##__VA_ARGS__)

#ifdef DEBUG
#define OvlDebug(msg, ...) OvlLOG(msg, "DEBUG", ##__VA_ARGS__)
#else
#define OvlDebug(msg, ...)
#endif

class ProcFileData
{
private:
    // Saved parameters
    char* m_path;           // Given path name
    size_t      m_size;         // Data size allocated

    // Working data
    int		m_fd;           // File descriptor: -1 until opened
    int     m_length;       // Amount read last time
    char*	m_data;         // Data read buffer

public:
    ProcFileData (const char path[] = NULL, size_t size = FILEBUFF);
    ~ProcFileData ();

    // Open or rewind, then reread the current contents of the file
    bool refresh ();

    // Close the file, forcing a re-open on the next access
    void close ();

    // Access to initialized data
    const char* path ()   const { return m_path; }
    size_t      size ()   const { return m_size; }
    void        set_path (const char* path)    { m_path = (char*)path; }

    // Read-only access to the data read
    const char* data ()   const { return m_data; }
    size_t      length () const { return m_length; }

    // Common method searches the data for the string 'name'
    // and returns 0 for not found or content following the name
    const char* data_after (const char* name) const;

    // Common method to return the value of a numeric string following a name
    OVLValue get_value (const char* name) const;
};


#endif /* PROCFILE_H */
