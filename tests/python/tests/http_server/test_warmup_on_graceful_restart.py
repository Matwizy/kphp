import os
import time

from python.lib.testcase import KphpServerAutoTestCase


class TestWarmUpOnGracefulRestart(KphpServerAutoTestCase):
    @classmethod
    def extra_class_setup(cls):
        cls.kphp_server.update_options({
            "--workers-num": 10,
            "-v": True,
        })

    def prepare_for_test(self, *, workers_part, instance_cache_part, timeout_sec):
        self.kphp_server.update_options({
            "--warmup-workers-part": workers_part,
            "--warmup-instance-cache-elements-part": instance_cache_part,
            "--warmup-timeout-sec": timeout_sec,
        })
        self.kphp_server.restart()
        return self.kphp_server.pid

    def trigger_kphp_to_store_element_in_instance_cache(self):
        resp = self.kphp_server.http_get(uri='/store-in-instance-cache')
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(resp.text, "1")

    def test_warmup_timeout_expired(self):
        timeout_sec = 1
        old_pid = self.prepare_for_test(workers_part=0.1, instance_cache_part=1, timeout_sec=timeout_sec)

        self.trigger_kphp_to_store_element_in_instance_cache()
        time.sleep(1) # to be sure master saved stat in shared memory
        self.kphp_server.start()
        time.sleep(timeout_sec * 4)

        # Can exit only on timeout because instance cache is always cold (new master stores to instance cache nothing)
        self.assertEqual(os.waitpid(old_pid, os.WNOHANG)[0], old_pid)

    def test_warmup_instance_cache_become_hot(self):
        old_pid = self.prepare_for_test(workers_part=0.1, instance_cache_part=0.001, timeout_sec=30)

        self.trigger_kphp_to_store_element_in_instance_cache()
        time.sleep(1) # to be sure master saved stat in shared memory
        self.kphp_server.start()

        time.sleep(5)
        # check it's still waiting while new master warms up
        self.assertEqual(os.waitpid(old_pid, os.WNOHANG)[0], 0)

        # only 1 new worker and 9 old workers will process this http requests => we need more requests
        for _ in range(200):
            self.trigger_kphp_to_store_element_in_instance_cache()

        time.sleep(4)
        # here it must be hot enough
        self.assertEqual(os.waitpid(old_pid, os.WNOHANG)[0], old_pid)
