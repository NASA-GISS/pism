def get(self, key):
    methods = [ # order matters
        self.get_flag,
        self.get_number,
        self.get_numbers,
        self.get_string,
    ]

    for method in methods:
        try:
            return method(key)
        except:
            pass

    return self._get(key)

def set(self, key, val):
    methods = [ # order matters
        self.set_flag,
        self.set_number,
        self.set_numbers,
        self.set_string,
    ]

    for method in methods:
        try:
            return method(key, val)
        except:
            pass

    return self._set(key, val)


def set_from_dict(self, d: dict):
    """
    Set config options from a python dictionary.
    """
    for key, val in d.items():
        self.set(key, val)
