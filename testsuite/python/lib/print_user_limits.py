import resource
print(resource.getrlimit(resource.RLIMIT_CORE)[0])
print(resource.getrlimit(resource.RLIMIT_FSIZE)[0])
print(resource.getrlimit(resource.RLIMIT_NOFILE)[0])

if resource.getrlimit(resource.RLIMIT_NPROC) != resource.error:
    print(resource.getrlimit(resource.RLIMIT_NPROC)[0])
else:
    print('USER_NPROC unsupported')
if resource.getrlimit(resource.RLIMIT_STACK) != resource.error:
    print(resource.getrlimit(resource.RLIMIT_STACK)[0])
else:
    print('USER_STACK unsupported')
