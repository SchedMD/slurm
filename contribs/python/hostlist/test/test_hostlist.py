from hostlist import expand_hostlist, collect_hostlist, BadHostlist
import unittest

class TestExpand1(unittest.TestCase):

    def expand_eq(self, hostlist, expanded_list):
        self.assertEqual(expand_hostlist(hostlist), expanded_list)

    def expand_sort_eq(self, hostlist, expanded_list):
        self.assertEqual(expand_hostlist(hostlist, sort=True), expanded_list)

    def expand_length(self, hostlist, expanded_length):
        self.assertEqual(len(expand_hostlist(hostlist)), expanded_length)

    def expand_bad(self, hostlist):
        self.assertRaises(BadHostlist, expand_hostlist,  hostlist)

    def test_expand(self):
        self.expand_eq("n[9-11]", ["n9", "n10", "n11"])
        self.expand_sort_eq("n[9-11]", ["n9", "n10", "n11"])
        self.expand_eq("n[09-11]", ["n09", "n10", "n11"])
        self.expand_eq("n[009-11]", ["n009", "n010", "n011"])
        self.expand_sort_eq("n[009-11]", ["n009", "n010", "n011"])
        self.expand_eq("n[009-011]", ["n009", "n010", "n011"])

        self.expand_eq("n[17-17]", ["n17"])
        self.expand_eq("n1,n3", ["n1", "n3"])
        self.expand_sort_eq("n1,n3", ["n1", "n3"])
        self.expand_eq("n3,n1", ["n3", "n1"])
        self.expand_sort_eq("n3,n1", ["n1", "n3"])
        self.expand_eq("n1,n3,n1", ["n1", "n3"])
        self.expand_sort_eq("n1,n3,n1", ["n1", "n3"])
        self.expand_eq("n3,n1,n3", ["n3", "n1"])
        self.expand_sort_eq("n3,n1,n3", ["n1", "n3"])

        self.expand_eq("n[1],n3", ["n1", "n3"])
        self.expand_eq("n[1,3]", ["n1", "n3"])
        self.expand_eq("n[3,1]", ["n3", "n1"])
        self.expand_sort_eq("n[3,1]", ["n1", "n3"])
        self.expand_eq("n[1,3,1]", ["n1", "n3"])

        self.expand_eq("n1,n2,n[9-11],n3", ["n1", "n2", "n9", "n10", "n11", "n3"])

        self.expand_eq("n[1-3]m[4-6]", ["n1m4", "n1m5", "n1m6",
                                        "n2m4", "n2m5", "n2m6",
                                        "n3m4", "n3m5", "n3m6"])
        self.expand_eq("n[1-2][4-5]m", ["n14m", "n15m",
                                        "n24m", "n25m"])
        self.expand_eq("[1-2][4-5]", ["14", "15",
                                      "24", "25"])

        self.expand_length("n[1-100]m[1-100]", 100*100)
        self.expand_length("[1-10][1-10][1-10]", 10*10*10)

        self.expand_eq("n[1-5,3-8]", ["n1", "n2", "n3", "n4", "n5", "n6", "n7", "n8"])
        self.expand_eq("n[3-8,1-5]", ["n3", "n4", "n5", "n6", "n7", "n8", "n1", "n2"])
        self.expand_sort_eq("n[3-8,1-5]", ["n1", "n2", "n3", "n4", "n5", "n6", "n7", "n8"])

        self.expand_eq("", [])

        self.expand_bad("n[]")
        self.expand_bad("n[-]")
        self.expand_bad("n[1-]")
        self.expand_bad("n[-1]")
        self.expand_bad("n[1,]")
        self.expand_bad("n[,1]")
        self.expand_bad("n[1-3,]")
        self.expand_bad("n[,1-3]")
        self.expand_bad("n[3-1]")
        self.expand_bad("n[")
        self.expand_bad("n]")
        self.expand_bad("n[[]]")
        self.expand_bad("n[1,[]]")
        self.expand_bad("n[x]")
        self.expand_bad("n[1-10x]")

        self.expand_bad("n[1-1000000]")
        self.expand_bad("n[1-1000][1-1000]")

    def collect_eq(self, hostlist, expanded_list):
        # Note the order of the arguments! This makes it easier to
        # copy tests between the expand and collect parts!
        self.assertEqual(hostlist, collect_hostlist(expanded_list))

    def test_collect(self):
        self.collect_eq("n[9-11]", ["n9", "n10", "n11"])
        self.collect_eq("n[09-11]", ["n09", "n10", "n11"])
        self.collect_eq("n[009-011]", ["n009", "n010", "n011"])

        self.collect_eq("n[1-3,9-11]", ["n1", "n2", "n9", "n10", "n11", "n3"])

        self.collect_eq("m1,n[9-11],p[7-8]", ["n9", "n10", "p7", "m1", "n11", "p8"])

        self.collect_eq("x[1-2]y[4-5]", ["x1y4", "x1y5",
                                         "x2y4", "x2y5"])
        self.collect_eq("[1-2]y[4-5]z", ["1y4z", "1y5z",
                                         "2y4z", "2y5z"])

        self.collect_eq("x1y[4-5],x2y4", ["x1y4", "x1y5", "x2y4"])
        self.collect_eq("x1y5,x2y[4-5]", ["x1y5", "x2y4", "x2y5"])
        self.collect_eq("x1y5,x2y4", ["x1y5", "x2y4"])

        self.collect_eq("", [""])

        self.collect_eq("n[9,09]", ["n09","n9"])
        self.collect_eq("n[9,09]", ["n9","n09"])
        self.collect_eq("n[9-10]", ["n9","n10"])
        self.collect_eq("n[09-10]", ["n09","n10"])
        self.collect_eq("n[009,10]", ["n009","n10"])

        self.collect_eq("x", ["x"])
        self.collect_eq("x", ["x", "x"])
        self.collect_eq("x,y", ["x", "y", "x"])

        self.collect_eq("n1", ["n1"])
        self.collect_eq("n1", ["n1", "n1"])
        self.collect_eq("n[1-2]", ["n1", "n2", "n1"])

        self.collect_eq("x,y[10-12],z", ["z","y10","y12", "x", "y11"])

    
if __name__ == '__main__':
    unittest.main()
