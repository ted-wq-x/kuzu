package io.transwarp.stellardb_booster;

import java.util.Map;

/**
 * BoosterNative is a wrapper class for the native library.
 * It is used to load the native library and call the native functions.
 * This class is not intended to be used by end users.
 */
public class BoosterNative {
    static {
        try {
//            String os_name = "";
//            String os_arch;
//            String os_name_detect = System.getProperty("os.name").toLowerCase().trim();
//            String os_arch_detect = System.getProperty("os.arch").toLowerCase().trim();
//            switch (os_arch_detect) {
//                case "x86_64":
//                case "amd64":
//                    os_arch = "amd64";
//                    break;
//                case "aarch64":
//                case "arm64":
//                    os_arch = "arm64";
//                    break;
//                case "i386":
//                    os_arch = "i386";
//                    break;
//                default:
//                    throw new IllegalStateException("Unsupported system architecture");
//            }
//            if (os_name_detect.startsWith("windows")) {
//                os_name = "windows";
//            } else if (os_name_detect.startsWith("mac")) {
//                os_name = "osx";
//            } else if (os_name_detect.startsWith("linux")) {
//                os_name = "linux";
//            }
//            String lib_res_name = "/libjava_native.so" + "_" + os_name + "_" + os_arch;
//
//            Path lib_file = Files.createTempFile("libjava_native", ".so");
//            URL lib_res = BoosterNative.class.getResource(lib_res_name);
//            if (lib_res == null) {
//                throw new IOException(lib_res_name + " not found");
//            }
//            Files.copy(lib_res.openStream(), lib_file, StandardCopyOption.REPLACE_EXISTING);
//            new File(lib_file.toString()).deleteOnExit();
//            String lib_path = lib_file.toAbsolutePath().toString();
//            System.load(lib_path);
//            if(os_name.equals("linux")) {
//                native_reload_library(lib_path);
//            }
            String libName = "stellar_booster";
            System.loadLibrary(libName);
        } catch (Throwable e) {
            e.printStackTrace();
        }
    }

    // Hack: Reload the native library again in JNI bindings to work around the 
    // extension loading issue on Linux as System.load() does not set 
    // `RTLD_GLOBAL` flag and there is no way to set it in Java.
    protected static native void native_reload_library(String lib_path);

    // Database
    protected static native long database_init(String database_path, long buffer_pool_size, boolean enable_compression, boolean read_only, long max_db_size);

    protected static native void database_destroy(BoosterDatabase db);

    protected static native void database_set_logging_level(String logging_level);

    // Connection
    protected static native long connection_init(BoosterDatabase database);

    protected static native void connection_destroy(BoosterConnection connection);

    protected static native void connection_set_max_num_thread_for_exec(
            BoosterConnection connection, long num_threads);

    protected static native long connection_get_max_num_thread_for_exec(BoosterConnection connection);

    protected static native BoosterQueryResult connection_query(BoosterConnection connection, String query);

    protected static native BoosterPreparedStatement connection_prepare(
            BoosterConnection connection, String query);

    protected static native BoosterQueryResult connection_execute(
            BoosterConnection connection, BoosterPreparedStatement prepared_statement, Map<String, BoosterValue> param);

    protected static native void connection_interrupt(BoosterConnection connection);

    protected static native void connection_set_query_timeout(
            BoosterConnection connection, long timeout_in_ms);

    // PreparedStatement
    protected static native void prepared_statement_destroy(BoosterPreparedStatement prepared_statement);

    protected static native boolean prepared_statement_allow_active_transaction(
            BoosterPreparedStatement prepared_statement);

    protected static native boolean prepared_statement_is_success(BoosterPreparedStatement prepared_statement);

    protected static native String prepared_statement_get_error_message(
            BoosterPreparedStatement prepared_statement);

    // QueryResult
    protected static native void query_result_destroy(BoosterQueryResult query_result);

    protected static native boolean query_result_is_success(BoosterQueryResult query_result);

    protected static native String query_result_get_error_message(BoosterQueryResult query_result);

    protected static native long query_result_get_num_columns(BoosterQueryResult query_result);

    protected static native String query_result_get_column_name(BoosterQueryResult query_result, long index);

    protected static native BoosterDataType query_result_get_column_data_type(
            BoosterQueryResult query_result, long index);

    protected static native long query_result_get_num_tuples(BoosterQueryResult query_result);

    protected static native BoosterQuerySummary query_result_get_query_summary(BoosterQueryResult query_result);

    protected static native boolean query_result_has_next(BoosterQueryResult query_result);

    protected static native BoosterFlatTuple query_result_get_next(BoosterQueryResult query_result);

    protected static native String query_result_to_string(BoosterQueryResult query_result);

    protected static native void query_result_reset_iterator(BoosterQueryResult query_result);

    // FlatTuple
    protected static native void flat_tuple_destroy(BoosterFlatTuple flat_tuple);

    protected static native BoosterValue flat_tuple_get_value(BoosterFlatTuple flat_tuple, long index);

    protected static native String flat_tuple_to_string(BoosterFlatTuple flat_tuple);

    // DataType
    protected static native long data_type_create(
            BoosterDataTypeID id, BoosterDataType child_type, long fixed_num_elements_in_list);

    protected static native BoosterDataType data_type_clone(BoosterDataType data_type);

    protected static native void data_type_destroy(BoosterDataType data_type);

    protected static native boolean data_type_equals(BoosterDataType data_type1, BoosterDataType data_type2);

    protected static native BoosterDataTypeID data_type_get_id(BoosterDataType data_type);

    protected static native BoosterDataType data_type_get_child_type(BoosterDataType data_type);

    protected static native long data_type_get_fixed_num_elements_in_list(BoosterDataType data_type);

    // Value
    protected static native BoosterValue value_create_null();

    protected static native BoosterValue value_create_null_with_data_type(BoosterDataType data_type);

    protected static native boolean value_is_null(BoosterValue value);

    protected static native void value_set_null(BoosterValue value, boolean is_null);

    protected static native BoosterValue value_create_default(BoosterDataType data_type);

    protected static native <T> long value_create_value(T val);

    protected static native BoosterValue value_clone(BoosterValue value);

    protected static native void value_copy(BoosterValue value, BoosterValue other);

    protected static native void value_destroy(BoosterValue value);

    protected static native long value_get_list_size(BoosterValue value);

    protected static native BoosterValue value_get_list_element(BoosterValue value, long index);

    protected static native BoosterDataType value_get_data_type(BoosterValue value);

    protected static native <T> T value_get_value(BoosterValue value);

    protected static native String value_to_string(BoosterValue value);

    protected static native BoosterInternalID node_val_get_id(BoosterValue node_val);

    protected static native BoosterInternalID rel_val_get_id(BoosterValue node_val);

    protected static native String node_val_get_label_name(BoosterValue node_val);

    protected static native long node_val_get_property_size(BoosterValue node_val);

    protected static native String node_val_get_property_name_at(BoosterValue node_val, long index);

    protected static native BoosterValue node_val_get_property_value_at(BoosterValue node_val, long index);

    protected static native String node_val_to_string(BoosterValue node_val);

    protected static native BoosterInternalID rel_val_get_src_id(BoosterValue rel_val);

    protected static native BoosterInternalID rel_val_get_dst_id(BoosterValue rel_val);

    protected static native String rel_val_get_label_name(BoosterValue rel_val);

    protected static native long rel_val_get_property_size(BoosterValue rel_val);

    protected static native String rel_val_get_property_name_at(BoosterValue rel_val, long index);

    protected static native BoosterValue rel_val_get_property_value_at(BoosterValue rel_val, long index);

    protected static native String rel_val_to_string(BoosterValue rel_val);

    protected static native String value_get_struct_field_name(BoosterValue struct_val, long index);

    protected static native long value_get_struct_index(BoosterValue struct_val, String field_name);

    protected static native BoosterDataType rdf_variant_get_data_type(BoosterValue rdf_variant);

    protected static native <T> T rdf_variant_get_value(BoosterValue rdf_variant);

    protected static native String get_version();

    protected static native long get_storage_version();
}
