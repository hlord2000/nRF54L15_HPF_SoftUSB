"""Offline tests; synthetic vectors are never hardware evidence."""
import unittest
from measure_csv import measure
from check_decode import check
from capture_usb import device_config


class AnalysisTests(unittest.TestCase):
    def test_sync_period_and_frequency_offset(self):
        period=1/1.5e6*1.001
        result=measure([i*period for i in range(10)],[i%2 for i in range(10)],1.5e6,True)
        self.assertAlmostEqual(result['estimated_bit_ns'],period*1e9)
        self.assertAlmostEqual(result['frequency_error_percent'],(1/1.001-1)*100)

    def test_nrzi_runs_not_individual_jitter(self):
        result=measure([i/1.5e6 for i in (0,1,3,6,7,13)], [0,1,0,1,0,1],1.5e6)
        self.assertIn('unresolved',result['measurement'])
        with self.assertRaises(ValueError):
            measure([i/1.5e6 for i in (0,1,3,6,7,13)], [0,1,0,1,0,1],1.5e6,True)

    def test_bad_capture_is_rejected(self):
        with self.assertRaises(ValueError):
            measure([0,1,2],[0,1,0],1.5e6)
        with self.assertRaises(ValueError):
            measure([0,0,1],[0,1,0],1.5e6)

    def test_decode_requires_nonempty_ordered_valid_output(self):
        expected={'patterns':['SYNC','ACK','EOP'],'min_rows':3}
        rows=[{'data':s} for s in ('SYNC','ACK','EOP')]
        self.assertEqual(check(rows,expected)['patterns'],3)
        for bad in ([],list(reversed(rows)), rows+[{'data':'CRC error'}]):
            with self.assertRaises(ValueError): check(bad,expected)
        with self.assertRaises(ValueError): check(rows,{'patterns':[]})

    def test_mso_configuration_uses_explicit_probe(self):
        cfg=device_config(1,0)
        self.assertEqual(cfg.digital_probes[0].port_index,1)
        self.assertEqual(len(cfg.digital_probes[0].channels),3)
        self.assertEqual(cfg.scope_channels,[])
        analog=device_config(0,100_000_000)
        self.assertEqual(len(analog.scope_channels),2)
        self.assertEqual(analog.scope_channels[0].analog.probe_attenuation,10)
        loop=device_config(0,0,1)
        self.assertEqual(len(loop.digital_probes),2)
        self.assertEqual([c.index for c in loop.digital_probes[0].channels],[0,1,2,3])
        self.assertEqual(loop.digital_probes[1].channels[0].index,0)
        with self.assertRaises(ValueError): device_config(0,0,0)


if __name__=='__main__':
    unittest.main()
