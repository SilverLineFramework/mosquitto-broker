import time, random, string

def time_ms():
    return time.time()*1000

def rand_str(N):
    return "".join(random.choice(string.ascii_lowercase+string.digits) for i in range(N))

def rand_num(N):
    return "".join(random.choice(string.digits) for i in range(N))
