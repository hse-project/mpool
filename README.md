# Mpool: Object Storage Media Pool

> **Notice**: [HSE](https://github.com/hse-project/hse) versions 2.0 and
> later do not make use of mpool.  As a result, mpool is no longer
> actively maintained.

**Mpool** is a Linux&reg; loadable kernel module
that implements an object storage device interface for SSDs and other
solid-state storage.
It provides a high-performance alternative to file systems or raw
block devices for applications that can benefit from its simple object
storage model and unique features.

Mpool is designed for

* Traditional block SSDs
* Emerging SSD interfaces, such as
NVMe [Zoned Namespaces](http://zonedstorage.io/) (ZNS), which
often expose controller or media behaviors that impose I/O constraints
* Persistent memory

Mpool insulates client applications from these storage device
and media details.

> Mpool currently supports traditional block SSDs only.

Mpool was originally developed for the
[HSE](https://github.com/hse-project/hse) storage engine,
but is made available as a separate project.


# Key Features

* Object storage model comprising immutable blocks (blobs) and appendable logs
* Objects can optionally be placed on multiple classes of solid-state storage
* Facilities to memory-map arbitrary collections of block objects into a
linear address space
* Proactive management of block object data in the Linux page cache based
on object-level usage metrics
* Simultaneously access block object data directly and memory-mapped with no
performance penalty
* Management model and CLI similar to that of Linux LVM
* C API library that can be embedded in any application


# Getting Started

The [mpool Wiki](https://github.com/hse-project/mpool/wiki)
contains all the information you need to get started with mpool.
