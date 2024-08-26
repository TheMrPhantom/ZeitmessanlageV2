import { CircularProgress } from '@mui/material'
import React from 'react'

type Props = {}

const Loader = (props: Props) => {
    return (
        <CircularProgress color="secondary" />
    )
}

export default Loader